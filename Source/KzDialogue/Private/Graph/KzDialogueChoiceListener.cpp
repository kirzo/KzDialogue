// Copyright 2026 kirzo

#include "Graph/KzDialogueChoiceListener.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Settings/KzDialogueSettings.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UKzDialogueChoiceListener* UKzDialogueChoiceListener::Create(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}

	UKzDialogueChoiceListener* Listener = NewObject<UKzDialogueChoiceListener>(Subsystem);
	Listener->Creator = WorldContextObject;
	Listener->bHasCreator = WorldContextObject != nullptr;
	Subsystem->AddChoiceListener(Listener);
	Subsystem->OnDialogueGraphStarted.AddDynamic(Listener, &UKzDialogueChoiceListener::HandleGraphStarted);
	return Listener;
}

UKzDialogueChoiceListener* UKzDialogueChoiceListener::CreateDialogueChoiceListener(UObject* WorldContextObject, const TArray<FGameplayTag>& InChannels, APlayerController* InPlayer)
{
	UKzDialogueChoiceListener* Listener = Create(WorldContextObject);
	if (!Listener)
	{
		return nullptr;
	}

	// The creating widget's player, else the first local one.
	if (!InPlayer)
	{
		if (const UUserWidget* Widget = Cast<UUserWidget>(WorldContextObject)) { InPlayer = Widget->GetOwningPlayer(); }
	}
	if (!InPlayer)
	{
		InPlayer = Listener->GetWorld() ? Listener->GetWorld()->GetFirstPlayerController() : nullptr;
	}
	Listener->Channels = InChannels;
	Listener->Player = InPlayer;

	// Already running: listened to, without events nobody could have bound yet.
	const TArray<TObjectPtr<UKzDialogueGraphInstance>> Running = CastChecked<UKzDialogueSubsystem>(Listener->GetOuter())->GetGraphInstances();
	for (UKzDialogueGraphInstance* Instance : Running)
	{
		if (Listener->Accepts(Instance)) { Listener->Listen(Instance); }
	}
	return Listener;
}

UKzDialogueChoiceListener* UKzDialogueChoiceListener::ListenToDialogueGraph(UObject* WorldContextObject, UKzDialogueGraphInstance* Instance)
{
	UKzDialogueChoiceListener* Listener = IsValid(Instance) ? Create(WorldContextObject) : nullptr;
	if (Listener)
	{
		Listener->bSingleInstance = true;
		Listener->Listen(Instance);
	}
	return Listener;
}

void UKzDialogueChoiceListener::StopListening()
{
	for (const TWeakObjectPtr<UKzDialogueGraphInstance>& Instance : Instances)
	{
		if (UKzDialogueGraphInstance* Listened = Instance.Get())
		{
			Listened->OnOptionsOffered.RemoveAll(this);
			Listened->OnOptionsWithdrawn.RemoveAll(this);
			Listened->OnEnded.RemoveAll(this);
		}
	}
	Instances.Reset();
	CurrentInstance.Reset();
	ChoosingInstance.Reset();

	if (UKzDialogueSubsystem* Subsystem = Cast<UKzDialogueSubsystem>(GetOuter()))
	{
		Subsystem->OnDialogueGraphStarted.RemoveAll(this);
		Subsystem->RemoveChoiceListener(this);
	}
}

bool UKzDialogueChoiceListener::ChooseOption(int32 Index)
{
	UKzDialogueGraphInstance* Instance = ChoosingInstance.Get();
	return Instance && Instance->SelectOption(Index);
}

void UKzDialogueChoiceListener::Next()
{
	if (UKzDialogueGraphInstance* Instance = CurrentInstance.Get())
	{
		Instance->Next();
	}
}

TArray<FKzDialogueGraphOption> UKzDialogueChoiceListener::GetOptions() const
{
	const UKzDialogueGraphInstance* Instance = ChoosingInstance.Get();
	return Instance ? Instance->GetOptions() : TArray<FKzDialogueGraphOption>();
}

float UKzDialogueChoiceListener::GetTimeLeft() const
{
	const UKzDialogueGraphInstance* Instance = ChoosingInstance.Get();
	return Instance ? Instance->GetTimeLeft() : 0.0f;
}

bool UKzDialogueChoiceListener::Accepts(const UKzDialogueGraphInstance* Instance) const
{
	const UKzDialoguePlayer* DialoguePlayer = Instance ? Instance->GetPlayer() : nullptr;
	if (!DialoguePlayer || !Instance->IsRunning())
	{
		return false;
	}
	if (!Channels.ContainsByPredicate([DialoguePlayer](const FGameplayTag& Scope) { return DialoguePlayer->Channel.MatchesTag(Scope); }))
	{
		return false;
	}

	// A per listener channel carries one player's conversation.
	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(DialoguePlayer->Channel);
	return !ChannelDef || ChannelDef->Scope != EKzDialogueChannelScope::PerListener || DialoguePlayer->GetOwner() == Player.Get();
}

void UKzDialogueChoiceListener::Listen(UKzDialogueGraphInstance* Instance)
{
	if (Instances.Contains(Instance))
	{
		return;
	}

	Instances.Add(Instance);
	CurrentInstance = Instance;
	Instance->OnOptionsOffered.AddDynamic(this, &UKzDialogueChoiceListener::HandleOptionsOffered);
	Instance->OnOptionsWithdrawn.AddDynamic(this, &UKzDialogueChoiceListener::HandleOptionsWithdrawn);
	Instance->OnEnded.AddDynamic(this, &UKzDialogueChoiceListener::HandleGraphEnded);
	if (!Instance->GetOptions().IsEmpty())
	{
		ChoosingInstance = Instance;
	}
}

void UKzDialogueChoiceListener::Unlisten(UKzDialogueGraphInstance* Instance)
{
	Instance->OnOptionsOffered.RemoveAll(this);
	Instance->OnOptionsWithdrawn.RemoveAll(this);
	Instance->OnEnded.RemoveAll(this);
	Instances.Remove(Instance);

	if (ChoosingInstance == Instance) { ChoosingInstance.Reset(); }
	if (CurrentInstance == Instance)
	{
		CurrentInstance = Instances.IsEmpty() ? TWeakObjectPtr<UKzDialogueGraphInstance>() : Instances.Last();
	}
}

void UKzDialogueChoiceListener::HandleGraphStarted(UKzDialogueGraphInstance* Instance)
{
	if (IsAbandoned())
	{
		StopListening();
		return;
	}
	// A listener of one play hears only its start.
	if (bSingleInstance ? Instances.Contains(Instance) : Accepts(Instance))
	{
		Listen(Instance);
		OnConversationStarted.Broadcast(Instance);
	}
}

void UKzDialogueChoiceListener::HandleOptionsOffered(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options)
{
	ChoosingInstance = Instance;
	OnShowOptions.Broadcast(Instance, Options);
}

void UKzDialogueChoiceListener::HandleOptionsWithdrawn(UKzDialogueGraphInstance* Instance)
{
	if (ChoosingInstance == Instance)
	{
		ChoosingInstance.Reset();
	}
	OnHideOptions.Broadcast(Instance);
}

void UKzDialogueChoiceListener::HandleGraphEnded(UKzDialogueGraphInstance* Instance, FName Exit, bool bInterrupted)
{
	Unlisten(Instance);
	OnConversationEnded.Broadcast(Instance);
}