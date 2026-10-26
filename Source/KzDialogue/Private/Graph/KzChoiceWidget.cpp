// Copyright 2026 kirzo

#include "Graph/KzChoiceWidget.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"

bool UKzChoiceWidget::ChooseOption(int32 Index)
{
	UKzDialogueGraphInstance* Instance = Shown.Get();
	return Instance && Instance->SelectOption(Index);
}

void UKzChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UWorld* World = GetWorld();
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	Subsystem->OnDialogueGraphStarted.AddDynamic(this, &UKzChoiceWidget::HandleGraphStarted);
	// A copy: showing options may run the Blueprint, which may choose one and end a play.
	const TArray<TObjectPtr<UKzDialogueGraphInstance>> Running = Subsystem->GetGraphInstances();
	for (UKzDialogueGraphInstance* Instance : Running)
	{
		Listen(Instance);
		if (Instance && !Instance->GetOptions().IsEmpty())
		{
			HandleOptionsOffered(Instance, Instance->GetOptions());
		}
	}
}

void UKzChoiceWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UKzDialogueSubsystem* Subsystem = World->GetSubsystem<UKzDialogueSubsystem>())
		{
			Subsystem->OnDialogueGraphStarted.RemoveAll(this);
		}
	}

	for (const TWeakObjectPtr<UKzDialogueGraphInstance>& Instance : Instances)
	{
		if (Instance.IsValid())
		{
			Instance->OnOptionsOffered.RemoveAll(this);
			Instance->OnOptionsWithdrawn.RemoveAll(this);
			Instance->OnEnded.RemoveAll(this);
		}
	}
	Instances.Reset();
	Shown.Reset();
	Super::NativeDestruct();
}

void UKzChoiceWidget::Listen(UKzDialogueGraphInstance* Instance)
{
	const UKzDialoguePlayer* Player = Instance ? Instance->GetPlayer() : nullptr;
	const bool bListened = Player && (ListenedChannels.IsEmpty() || ListenedChannels.ContainsByPredicate([Player](const FGameplayTag& Scope) { return Player->Channel.MatchesTag(Scope); }));
	if (!bListened || Instances.Contains(Instance))
	{
		return;
	}

	Instances.Add(Instance);
	Instance->OnOptionsOffered.AddDynamic(this, &UKzChoiceWidget::HandleOptionsOffered);
	Instance->OnOptionsWithdrawn.AddDynamic(this, &UKzChoiceWidget::HandleOptionsWithdrawn);
	Instance->OnEnded.AddDynamic(this, &UKzChoiceWidget::HandleGraphEnded);
}

void UKzChoiceWidget::HandleGraphStarted(UKzDialogueGraphInstance* Instance)
{
	Listen(Instance);
}

void UKzChoiceWidget::HandleOptionsOffered(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options)
{
	if (Instances.Contains(Instance))
	{
		Shown = Instance;
		ReceiveShowOptions(Instance, Options);
	}
}

void UKzChoiceWidget::HandleOptionsWithdrawn(UKzDialogueGraphInstance* Instance)
{
	if (Shown == Instance)
	{
		Shown.Reset();
		ReceiveHideOptions(Instance);
	}
}

void UKzChoiceWidget::HandleGraphEnded(UKzDialogueGraphInstance* Instance, FName Exit, bool bInterrupted)
{
	Instances.Remove(Instance);
}