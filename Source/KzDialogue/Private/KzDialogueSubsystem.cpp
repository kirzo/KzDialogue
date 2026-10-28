// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialogue.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "Graph/KzDialogueChoiceListener.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "Settings/KzDialogueSettings.h"
#include "KzDialogueMemorySubsystem.h"
#include "KzSpeakerAsset.h"
#include "KzDialogueSpeakerComponent.h"
#include "Net/KzDialogueNetActor.h"
#include "Net/KzDialogueNetComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Algo/RandomShuffle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"

void UKzDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Pull defaults from project settings; fall back to a sensible hardcoded tag.
	if (const UKzDialogueSettings* Settings = UKzDialogueSettings::Get())
	{
		DefaultChannel = Settings->DefaultChannel;
	}
	if (!DefaultChannel.IsValid())
	{
		DefaultChannel = Kz::Tags::Dialogue::MainChannel;
	}

	if (UWorld* World = GetWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UKzDialogueSubsystem::HandleActorSpawned));
	}
}

void UKzDialogueSubsystem::Deinitialize()
{
	bDeinitializing = true;

	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	// Copies end here quietly: the server's players are not this machine's to interrupt.
	for (UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player) && Player->IsProxy()) { Player->EndProxy(); }
	}

	// Tear down asset sessions BEFORE the players they drive. Iterate a copy: Interrupt() finishes the
	// session, which calls ReleaseAssetSession and mutates AssetSessions mid-loop.
	TArray<TObjectPtr<UKzDialogueAssetSession>> SessionsToTearDown = AssetSessions;
	for (UKzDialogueAssetSession* Session : SessionsToTearDown)
	{
		if (Session)
		{
			Session->Interrupt();
		}
	}
	AssetSessions.Reset();

	// Hard stop: no exit animations, and Admit refuses whatever the finish handlers request now.
	InterruptAll();
	Players.Reset();
	GraphInstances.Reset();
	Super::Deinitialize();
}

UKzDialoguePlayer* UKzDialogueSubsystem::GetOrCreatePlayer(FGameplayTag InChannel, AActor* Owner)
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	AActor* ResolvedOwner = ResolveOwner(InChannel, Owner);
	if (UKzDialoguePlayer* Existing = FindResolvedPlayer(InChannel, ResolvedOwner))
	{
		return Existing;
	}

	if (!UKzDialogueSettings::Get()->FindChannel(InChannel))
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Channel '%s' has no definition for it or any ancestor in Project Settings -> Plugins -> KzDialogue -> Channels; using fallback values."), *InChannel.ToString());
	}

	UKzDialoguePlayer* NewPlayer = NewObject<UKzDialoguePlayer>(this);
	NewPlayer->Channel = InChannel;
	NewPlayer->Owner = ResolvedOwner;
	NewPlayer->OnDialogueFinished.AddDynamic(this, &UKzDialogueSubsystem::HandlePlayerFinished);
	Players.Add(NewPlayer);

	// An owned player goes with its owner.
	if (ResolvedOwner)
	{
		ResolvedOwner->OnEndPlay.AddUniqueDynamic(this, &UKzDialogueSubsystem::HandleOwnerEndPlay);
	}

	// Broadcast BEFORE anything plays on it, so views can bind and subscribe to its request events
	// ahead of the first StartDialogue.
	OnPlayerCreated.Broadcast(InChannel, NewPlayer);

	return NewPlayer;
}

AActor* UKzDialogueSubsystem::ResolveOwner(FGameplayTag InChannel, AActor* Owner, const UKzSpeakerAsset* Speaker) const
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const EKzDialogueChannelScope Scope = ChannelDef ? ChannelDef->Scope : EKzDialogueChannelScope::Global;

	if (Scope == EKzDialogueChannelScope::PerSpeaker)
	{
		if (IsValid(Owner)) { return Owner; }

		// Without an owner, the actor of the line's speaker; without one, the shared player.
		const UKzDialogueSpeakerComponent* SpeakerComponent = Speaker ? UKzDialogueSpeakerComponent::FindSpeaker(this, Speaker) : nullptr;
		return SpeakerComponent ? SpeakerComponent->GetOwner() : nullptr;
	}

	if (Scope == EKzDialogueChannelScope::PerListener)
	{
		// The player, not their pawn, which can change.
		if (APlayerController* Controller = Cast<APlayerController>(Owner)) { return Controller; }
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController())) { return Controller; }
		}
		const UWorld* World = GetWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	return nullptr;
}

UKzDialoguePlayer* UKzDialogueSubsystem::FindResolvedPlayer(FGameplayTag InChannel, const AActor* ResolvedOwner) const
{
	for (UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player) && Player->Channel == InChannel && Player->Owner == ResolvedOwner)
		{
			return Player;
		}
	}
	return nullptr;
}

UKzDialoguePlayer* UKzDialogueSubsystem::GetPlayerForRequest(FGameplayTag InChannel, AActor* Owner, const UKzSpeakerAsset* Speaker)
{
	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel, ResolveOwner(InChannel, Owner, Speaker));
	PendingRequest.Channel = InChannel;
	PendingRequest.Owner = DescribeOwner(Player);
	return Player;
}

FString UKzDialogueSubsystem::DescribeOwner(const UKzDialoguePlayer* Player)
{
	const AActor* Owner = Player ? Player->GetOwner() : nullptr;
	return Owner ? Owner->GetActorNameOrLabel() : FString();
}

void UKzDialogueSubsystem::HandleOwnerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	// Collected first: interrupting runs finish listeners, which may play again.
	TArray<UKzDialoguePlayer*> Owned;
	for (UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player) && Player->Owner == Actor) { Owned.Add(Player); }
	}

	for (UKzDialoguePlayer* Player : Owned)
	{
		RemovePlayer(*Player);
	}
}

void UKzDialogueSubsystem::RemovePlayer(UKzDialoguePlayer& Player)
{
	DropQueue(&Player, TEXT("its owner went away"));
	if (Player.IsProxy()) { Player.EndProxy(); }
	else { Player.Interrupt(); }

	if (FKzDialogueNetEntries* Entries = IsNetServer() ? FindNetEntries(Player, false) : nullptr)
	{
		Entries->Remove(Player.Channel, Player.GetOwner());
	}

	// A copy of a play on this player goes with it.
	TArray<UKzDialogueGraphInstance*> Copies;
	for (UKzDialogueGraphInstance* Instance : GraphInstances)
	{
		if (Instance && Instance->IsProxy() && Instance->GetPlayer() == &Player) { Copies.Add(Instance); }
	}
	for (UKzDialogueGraphInstance* Copy : Copies)
	{
		Copy->EndProxy();
	}

	Players.Remove(&Player);
	Queues.Remove(&Player);
	PlayingRequests.Remove(&Player);
	OnPlayerRemoved.Broadcast(Player.Channel, &Player);
}

// ---------------------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------------------

bool UKzDialogueSubsystem::IsNetServer() const
{
	const UWorld* World = GetWorld();
	const ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
	return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}

bool UKzDialogueSubsystem::IsNetClient() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_Client;
}

void UKzDialogueSubsystem::HandleActorSpawned(AActor* Actor)
{
	APlayerController* Controller = Cast<APlayerController>(Actor);
	if (Controller && IsNetServer())
	{
		FindOrAddNetComponent(Controller);
	}
}

UKzDialogueNetComponent* UKzDialogueSubsystem::FindOrAddNetComponent(APlayerController* Controller)
{
	if (!IsValid(Controller)) { return nullptr; }

	// The subsystem adds exactly one, so the class finds it.
	if (UKzDialogueNetComponent* Existing = Controller->FindComponentByClass<UKzDialogueNetComponent>())
	{
		return Existing;
	}

	UKzDialogueNetComponent* Component = NewObject<UKzDialogueNetComponent>(Controller, TEXT("KzDialogueNet"));
	Component->RegisterComponent();
	return Component;
}

UObject* UKzDialogueSubsystem::FindNetHost(const UKzDialoguePlayer& Player, bool bCreate)
{
	// A player's own dialogue goes to that player alone; the host's stays on this machine.
	if (APlayerController* Controller = Cast<APlayerController>(Player.GetOwner()))
	{
		if (Controller->IsLocalController()) { return nullptr; }
		return bCreate ? FindOrAddNetComponent(Controller) : Controller->FindComponentByClass<UKzDialogueNetComponent>();
	}

	if (!IsValid(NetActor) && bCreate && !bDeinitializing && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		NetActor = GetWorld()->SpawnActor<AKzDialogueNetActor>(Params);
	}
	return IsValid(NetActor) ? NetActor.Get() : nullptr;
}

FKzDialogueNetEntries* UKzDialogueSubsystem::FindNetEntries(const UKzDialoguePlayer& Player, bool bCreate)
{
	UObject* Host = FindNetHost(Player, bCreate);
	if (AKzDialogueNetActor* Actor = Cast<AKzDialogueNetActor>(Host)) { return &Actor->Entries; }
	UKzDialogueNetComponent* Component = Cast<UKzDialogueNetComponent>(Host);
	return Component ? &Component->Entries : nullptr;
}

FKzDialogueNetGraphEntries* UKzDialogueSubsystem::FindNetGraphEntries(const UKzDialoguePlayer& Player, bool bCreate)
{
	UObject* Host = FindNetHost(Player, bCreate);
	if (AKzDialogueNetActor* Actor = Cast<AKzDialogueNetActor>(Host)) { return &Actor->GraphEntries; }
	UKzDialogueNetComponent* Component = Cast<UKzDialogueNetComponent>(Host);
	return Component ? &Component->GraphEntries : nullptr;
}

UKzDialogueGraphInstance* UKzDialogueSubsystem::FindGraphCopy(int32 PlayId) const
{
	const TObjectPtr<UKzDialogueGraphInstance>* Found = GraphInstances.FindByPredicate([PlayId](const UKzDialogueGraphInstance* Instance)
	{
		return Instance && Instance->IsProxy() && Instance->GetNetPlayId() == PlayId;
	});
	return Found ? Found->Get() : nullptr;
}

void UKzDialogueSubsystem::PublishGraph(const UKzDialogueGraphInstance& Instance)
{
	const UKzDialoguePlayer* Player = Instance.GetPlayer();
	FKzDialogueNetGraphEntries* Entries = IsNetServer() && Player && Instance.GetNetPlayId() != 0 ? FindNetGraphEntries(*Player, true) : nullptr;
	if (!Entries) { return; }

	FKzDialogueNetGraphEntry& Entry = Entries->FindOrAdd(Instance.GetNetPlayId());
	Entry.Channel = Player->Channel;
	Entry.Owner = Player->GetOwner();
	Entry.bHasOwner = Entry.Owner != nullptr;
	Instance.FillNetEntry(Entry);
	Entries->Commit(Entry);
}

void UKzDialogueSubsystem::UnpublishGraph(const UKzDialogueGraphInstance& Instance)
{
	const UKzDialoguePlayer* Player = Instance.GetPlayer();
	if (FKzDialogueNetGraphEntries* Entries = IsNetServer() && Player && Instance.GetNetPlayId() != 0 ? FindNetGraphEntries(*Player, false) : nullptr)
	{
		Entries->Remove(Instance.GetNetPlayId());
	}
}

void UKzDialogueSubsystem::ReceiveNetGraphEntry(const FKzDialogueNetGraphEntry& Entry)
{
	if (!IsNetClient() || (Entry.bHasOwner && !IsValid(Entry.Owner))) { return; }

	UKzDialogueGraphInstance* Instance = FindGraphCopy(Entry.PlayId);
	if (!Instance)
	{
		Instance = NewObject<UKzDialogueGraphInstance>(this);
	}
	Instance->ApplyNetEntry(Entry);
}

void UKzDialogueSubsystem::ReleaseNetGraphEntry(const FKzDialogueNetGraphEntry& Entry)
{
	if (UKzDialogueGraphInstance* Instance = IsNetClient() ? FindGraphCopy(Entry.PlayId) : nullptr)
	{
		Instance->EndProxy();
	}
}

void UKzDialogueSubsystem::SendSelectOption(const UKzDialogueGraphInstance& Instance, int32 Index)
{
	const UKzDialoguePlayer* Player = Instance.GetPlayer();
	if (UKzDialogueNetComponent* Component = Player ? FindLocalNetComponent(*Player) : nullptr)
	{
		Component->ServerSelectOption(Instance.GetNetPlayId(), Index);
	}
}

void UKzDialogueSubsystem::HandleNetSelectOption(APlayerController* From, int32 PlayId, int32 Index)
{
	const TObjectPtr<UKzDialogueGraphInstance>* Found = GraphInstances.FindByPredicate([PlayId](const UKzDialogueGraphInstance* Instance)
	{
		return Instance && !Instance->IsProxy() && Instance->GetNetPlayId() == PlayId;
	});
	UKzDialogueGraphInstance* Instance = Found ? Found->Get() : nullptr;
	if (!Instance || !From) { return; }

	// Another player's own conversation is not theirs to answer; a shared one is anyone's, and the first answer wins.
	const UKzDialoguePlayer* Player = Instance->GetPlayer();
	const APlayerController* OwnerController = Player ? Cast<APlayerController>(Player->GetOwner()) : nullptr;
	if (OwnerController && OwnerController != From)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Dialogue graph play %d: %s tried to answer %s's conversation; refused."), PlayId, *From->GetName(), *OwnerController->GetName());
		return;
	}
	Instance->SelectOption(Index);
}

void UKzDialogueSubsystem::PublishPlayer(const UKzDialoguePlayer& Player, const FKzDialogueLine* NewLine)
{
	FKzDialogueNetEntries* Entries = IsNetServer() ? FindNetEntries(Player, true) : nullptr;
	if (!Entries) { return; }

	FKzDialogueNetEntry& Entry = Entries->FindOrAdd(Player.Channel, Player.GetOwner());
	Player.FillNetEntry(Entry);
	if (NewLine) { Entry.Line = FKzDialogueNetLine(*NewLine); }
	Entries->Commit(Entry);
}

void UKzDialogueSubsystem::ReceiveNetEntry(const FKzDialogueNetEntry& Entry)
{
	// Its owner is not relevant to this client (or not yet): nothing of it shows here.
	if (!IsNetClient() || (Entry.bHasOwner && !IsValid(Entry.Owner))) { return; }

	UKzDialoguePlayer* Player = GetOrCreatePlayer(Entry.Channel, Entry.Owner);
	if (!IsValid(Player)) { return; }

	if (!Player->IsProxy())
	{
		// The server's dialogue wins over one this machine started on its own.
		DropQueue(Player, TEXT("the server took the channel"));
		Player->BecomeProxy();
	}
	Player->ApplyNetEntry(Entry);
}

void UKzDialogueSubsystem::ReleaseNetEntry(const FKzDialogueNetEntry& Entry)
{
	UKzDialoguePlayer* Player = IsNetClient() ? FindResolvedPlayer(Entry.Channel, Entry.Owner) : nullptr;
	if (IsValid(Player) && Player->IsProxy())
	{
		RemovePlayer(*Player);
	}
}

UKzDialogueNetComponent* UKzDialogueSubsystem::FindLocalNetComponent(const UKzDialoguePlayer& Player) const
{
	APlayerController* Controller = Cast<APlayerController>(Player.GetOwner());
	if (!Controller || !Controller->IsLocalController())
	{
		Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	}

	UKzDialogueNetComponent* Component = Controller ? Controller->FindComponentByClass<UKzDialogueNetComponent>() : nullptr;
	if (!Component)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Channel '%s': no way to the server, the local player controller has no dialogue net component yet."), *Player.Channel.ToString());
	}
	return Component;
}

void UKzDialogueSubsystem::SendNext(const UKzDialoguePlayer& Player)
{
	if (UKzDialogueNetComponent* Component = FindLocalNetComponent(Player))
	{
		Component->ServerNext(Player.Channel, Player.GetOwner());
	}
}

void UKzDialogueSubsystem::SendViewPhaseDone(const UKzDialoguePlayer& Player, EKzDialogueState Phase, int32 DialogueSerial, int32 LineSerial)
{
	if (UKzDialogueNetComponent* Component = FindLocalNetComponent(Player))
	{
		Component->ServerViewPhaseDone(Player.Channel, Player.GetOwner(), Phase, DialogueSerial, LineSerial);
	}
}

void UKzDialogueSubsystem::HandleNetViewPhaseDone(APlayerController* From, FGameplayTag InChannel, AActor* Owner, EKzDialogueState Phase, int32 DialogueSerial, int32 LineSerial)
{
	// Only the player whose own dialogue it is answers for its views.
	UKzDialoguePlayer* Player = FindResolvedPlayer(InChannel, Owner);
	if (IsValid(Player) && From && Player->GetOwner() == From)
	{
		Player->HandleRemoteViewPhaseDone(Phase, DialogueSerial, LineSerial);
	}
}

double UKzDialogueSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void UKzDialogueSubsystem::HandleNetNext(APlayerController* From, FGameplayTag InChannel, AActor* Owner)
{
	UKzDialoguePlayer* Player = FindResolvedPlayer(InChannel, Owner);
	if (!IsValid(Player) || !From) { return; }

	// Another player's own dialogue is not theirs to advance; a shared one is anyone's.
	const APlayerController* OwnerController = Cast<APlayerController>(Player->GetOwner());
	if (OwnerController && OwnerController != From)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Channel '%s': %s asked to advance %s's dialogue; refused."), *InChannel.ToString(), *From->GetName(), *OwnerController->GetName());
		return;
	}
	Player->Next();
}

bool UKzDialogueSubsystem::IsAtConcurrencyLimit(const UKzDialoguePlayer& Player, const FKzDialogueChannelDefinition* ChannelDef) const
{
	if (!ChannelDef || ChannelDef->MaxConcurrent <= 0) { return false; }

	// Every player whose channel uses this definition counts: subchannels share their ancestor's limit.
	int32 Playing = 0;
	for (const UKzDialoguePlayer* Other : Players)
	{
		const bool bBusy = IsValid(Other) && Other != &Player && (Other->IsPlaying() || IsValid(Other->GetProvider()));
		if (bBusy && UKzDialogueSettings::Get()->FindChannel(Other->Channel) == ChannelDef)
		{
			++Playing;
		}
	}
	return Playing >= ChannelDef->MaxConcurrent;
}

void UKzDialogueSubsystem::GetPlayersInScope(FGameplayTag Scope, TArray<UKzDialoguePlayer*>& OutPlayers) const
{
	OutPlayers.Reset();
	if (!Scope.IsValid()) { Scope = DefaultChannel; }

	for (UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player) && Player->Channel.MatchesTag(Scope))
		{
			OutPlayers.Add(Player);
		}
	}
}

bool UKzDialogueSubsystem::IsSpeakerTalking(const UKzSpeakerAsset* Speaker, FGameplayTag ExceptChannel) const
{
	if (!Speaker) { return false; }

	for (const UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player) && Player->Channel != ExceptChannel && Player->IsPlaying() && Player->GetCurrentLine().Speaker.Asset == Speaker)
		{
			return true;
		}
	}
	return false;
}

// The audio's SoundClass without loading the wave when it is not loaded yet: the asset registry tags it.
static const USoundClass* FindAudioSoundClass(const TSoftObjectPtr<USoundBase>& Audio)
{
	if (const USoundBase* Loaded = Audio.Get()) { return Loaded->GetSoundClass(); }

	const FAssetData Data = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetByObjectPath(Audio.ToSoftObjectPath());
	FString ClassPath;
	if (!Data.GetTagValue(GET_MEMBER_NAME_CHECKED(USoundBase, SoundClassObject), ClassPath))
	{
		const USoundBase* Sound = Audio.LoadSynchronous();
		return Sound ? Sound->GetSoundClass() : nullptr;
	}

	// An unset class falls back to the project default, as USoundBase::GetSoundClass does.
	const FSoftObjectPath Path(FPackageName::ExportTextPathToObjectPath(ClassPath));
	return Path.IsValid() ? TSoftObjectPtr<USoundClass>(Path).LoadSynchronous() : GetDefault<UAudioSettings>()->GetDefaultSoundClass();
}

// Line-level resolution: the line's own channel, else the channel mapped to its audio's
// SoundClass in project settings. Empty when neither applies: the chain moves to the asset.
// The SoundClass step is skipped when the line has no audio or settings don't map its class
// (invalid mappings also count as unmapped).
static FGameplayTag ResolveLineLevelChannel(const FKzDialogueLine& Line)
{
	if (Line.DefaultChannel.IsValid()) { return Line.DefaultChannel; }

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	if (Line.Audio.IsNull() || !Settings || Settings->SoundClassChannels.IsEmpty()) { return FGameplayTag(); }

	return Settings->FindChannelForSoundClass(FindAudioSoundClass(Line.Audio));
}

// Returns the channel shared by every resolvable line of the alias, or empty when they disagree.
static FGameplayTag GetAliasUnanimousChannel(const UKzDialogueAsset& Asset, const FKzDialogueAlias& Alias)
{
	FGameplayTag Unanimous;
	bool bFirst = true;

	for (const FGuid& LineId : Alias.LineIds)
	{
		const FKzDialogueLine* Line = Asset.FindLine(LineId);
		if (!Line) { continue; }

		if (bFirst)
		{
			Unanimous = Line->DefaultChannel;
			bFirst = false;
		}
		else if (Line->DefaultChannel != Unanimous)
		{
			return FGameplayTag();
		}
	}

	return Unanimous;
}

FGameplayTag UKzDialogueSubsystem::ResolveChannelForEntry(FGameplayTag ExplicitChannel, const UKzDialogueAsset* Asset, FGuid EntryId) const
{
	if (ExplicitChannel.IsValid()) { return ExplicitChannel; }

	if (IsValid(Asset) && EntryId.IsValid())
	{
		// Line entry: the line's own channel, else its audio's SoundClass mapping.
		if (const FKzDialogueLine* Line = Asset->FindLine(EntryId))
		{
			const FGameplayTag LineLevel = ResolveLineLevelChannel(*Line);
			if (LineLevel.IsValid())
			{
				return LineLevel;
			}
		}

		// Alias entry: the alias's own channel, else its lines' unanimous one.
		if (const FKzDialogueAlias* Alias = Asset->FindAlias(EntryId))
		{
			if (Alias->DefaultChannel.IsValid())
			{
				return Alias->DefaultChannel;
			}

			const FGameplayTag Unanimous = GetAliasUnanimousChannel(*Asset, *Alias);
			if (Unanimous.IsValid())
			{
				return Unanimous;
			}
		}
	}

	if (IsValid(Asset) && Asset->DefaultChannel.IsValid())
	{
		return Asset->DefaultChannel;
	}

	return DefaultChannel;
}

UKzDialoguePlayer* UKzDialogueSubsystem::FindPlayer(FGameplayTag InChannel, AActor* Owner) const
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }
	return FindResolvedPlayer(InChannel, ResolveOwner(InChannel, Owner));
}


int32 UKzDialogueSubsystem::ResolvePriority(int32 RequestedPriority, int32 AssetHintPriority, const FKzDialogueChannelDefinition* ChannelDef) const
{
	int32 Resolved;
	if (RequestedPriority != InheritPriority) { Resolved = RequestedPriority; }
	else if (AssetHintPriority != InheritPriority) { Resolved = AssetHintPriority; }
	else if (ChannelDef) { Resolved = ChannelDef->DefaultPriority; }
	else { Resolved = 0; }

	return Resolved;
}

EKzDialogueAdvanceMode UKzDialogueSubsystem::ResolveAdvanceMode(EKzDialogueAdvanceMode Override, const UKzDialogueAsset* Asset) const
{
	if (Override != EKzDialogueAdvanceMode::Inherit) { return Override; }
	if (IsValid(Asset) && Asset->AdvanceMode != EKzDialogueAdvanceMode::Inherit) { return Asset->AdvanceMode; }
	return EKzDialogueAdvanceMode::Automatic;
}

EKzDialoguePlayResult UKzDialogueSubsystem::Admit(UKzDialoguePlayer& Player, int32 Priority, const FKzDialogueChannelDefinition* ChannelDef) const
{
	const FKzDialogueChannelQueue* Queue = Queues.Find(&Player);
	const bool bOthersWaiting = Queue && Queue->Entries.Num() > 0;

	EKzDialoguePlayResult Result = EKzDialoguePlayResult::Started;
	if (bDeinitializing)
	{
		Result = EKzDialoguePlayResult::RejectedTearingDown;
	}
	else if (Player.IsProxy())
	{
		Result = EKzDialoguePlayResult::RejectedServerChannel;
	}
	else if ((!Player.IsPlaying() && !IsValid(Player.GetProvider())) || Player.GetState() == EKzDialogueState::Exiting)
	{
		// Free, or only its exit animations are left; requests already waiting still go first, and other owners may fill the channel.
		if (bOthersWaiting) { Result = EKzDialoguePlayResult::Queued; }
		else if (IsAtConcurrencyLimit(Player, ChannelDef)) { Result = EKzDialoguePlayResult::RejectedConcurrencyLimit; }
		else { return EKzDialoguePlayResult::Started; }
	}
	else if (Player.CurrentPriority > Priority)
	{
		Result = EKzDialoguePlayResult::RejectedPriority;
	}
	else if ((ChannelDef && !ChannelDef->bAllowInterruption) || !Player.bCurrentInterruptible)
	{
		Result = EKzDialoguePlayResult::RejectedNotInterruptible;
	}
	else
	{
		return EKzDialoguePlayResult::Started;
	}

	// The busy policy may keep a refused request waiting.
	if (Result != EKzDialoguePlayResult::RejectedTearingDown && Result != EKzDialoguePlayResult::RejectedServerChannel && ChannelDef && ChannelDef->BusyPolicy != EKzChannelBusyPolicy::Reject)
	{
		const bool bFull = ChannelDef->BusyPolicy == EKzChannelBusyPolicy::Queue && bOthersWaiting && Queue->Entries.Num() >= ChannelDef->MaxQueueLength;
		Result = bFull ? EKzDialoguePlayResult::RejectedQueueFull : EKzDialoguePlayResult::Queued;
	}

	if (Result != EKzDialoguePlayResult::Queued)
	{
		UE_LOG(LogKzDialogue, Verbose, TEXT("Channel '%s' refused a priority %d request: %s."), *Player.Channel.ToString(), Priority, *UEnum::GetValueAsString(Result));
	}
	return Result;
}

// History label of a line or alias of Asset: readable in one log line.
static FString DescribeEntry(const UKzDialogueAsset* Asset, const FGuid& EntryId, int32 Count = 1)
{
	FString Label;
	if (!Asset) { Label = TEXT("(no asset)"); }
	else if (const FKzDialogueAlias* Alias = Asset->FindAlias(EntryId)) { Label = FString::Printf(TEXT("%s: alias %s"), *Asset->GetName(), *Alias->AliasName.ToString()); }
	else if (const FKzDialogueLine* Line = Asset->FindLine(EntryId)) { Label = FString::Printf(TEXT("%s: \"%s\""), *Asset->GetName(), *Line->Text.ToString().Left(48)); }
	else { Label = FString::Printf(TEXT("%s: missing entry %s"), *Asset->GetName(), *EntryId.ToString()); }
	return Count > 1 ? FString::Printf(TEXT("%s (+%d more)"), *Label, Count - 1) : Label;
}

void UKzDialogueSubsystem::AddHistory(FGameplayTag Channel, const FString& Owner, int32 Priority, const FString& Request, const FString& Outcome)
{
	if (History.Num() >= HistorySize) { History.RemoveAt(0); }
	History.Add({ GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, Channel, Priority, Request.IsEmpty() ? FString(TEXT("(unknown)")) : Request, Outcome, Owner });
}

UKzDialoguePlayer* UKzDialogueSubsystem::Record(EKzDialoguePlayResult Result, UKzDialoguePlayer* Player)
{
	// A start that came back empty: a listener of the ending took the channel first.
	if (Result == EKzDialoguePlayResult::Started && !Player) { Result = EKzDialoguePlayResult::RejectedPriority; }

	LastPlayResult = Result;
	LastQueuedRequestId.Invalidate();
	const bool bPlays = Result == EKzDialoguePlayResult::Started || Result == EKzDialoguePlayResult::Joined || Result == EKzDialoguePlayResult::Queued;

	// A busy refusal reads with what holds the channel.
	FString Outcome = UEnum::GetDisplayValueAsText(Result).ToString();
	const bool bBusy = Result == EKzDialoguePlayResult::RejectedPriority || Result == EKzDialoguePlayResult::RejectedNotInterruptible || Result == EKzDialoguePlayResult::RejectedQueueFull;
	const FString* Holder = Player ? PlayingRequests.Find(Player) : nullptr;
	if (bBusy && Holder)
	{
		Outcome += FString::Printf(TEXT(" (busy with %s at priority %d)"), **Holder, Player->CurrentPriority);
	}
	AddHistory(PendingRequest.Channel, PendingRequest.Owner, PendingRequest.Priority, PendingRequest.Request, Outcome);
	if (Result == EKzDialoguePlayResult::Started) { PlayingRequests.Add(Player, PendingRequest.Request); }

	return bPlays ? Player : nullptr;
}

UKzDialoguePlayer* UKzDialogueSubsystem::Enqueue(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef)
{
	// Replace keeps only the newest request; what was waiting is reported dropped before the queue changes again.
	if (ChannelDef && ChannelDef->BusyPolicy == EKzChannelBusyPolicy::Replace)
	{
		DropQueue(&Player, TEXT("replaced by a newer request"));
	}

	FKzQueuedDialogue Entry;
	Entry.Provider = Provider;
	Entry.RequestId = FGuid::NewGuid();
	Entry.Priority = Priority;
	Entry.bInterruptible = bInterruptible;
	Entry.AdvanceMode = AdvanceMode;
	Entry.QueuedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Entry.HistoryLabel = PendingRequest.Request;

	// Higher priority first; equal priorities keep arrival order.
	TArray<FKzQueuedDialogue>& Entries = Queues.FindOrAdd(&Player).Entries;
	const int32 Index = Entries.IndexOfByPredicate([Priority](const FKzQueuedDialogue& Waiting) { return Waiting.Priority < Priority; });
	Entries.Insert(Entry, Index == INDEX_NONE ? Entries.Num() : Index);
	UE_LOG(LogKzDialogue, Verbose, TEXT("Channel '%s' queued a priority %d request (%d waiting)."), *Player.Channel.ToString(), Priority, Entries.Num());

	// A free player has no finish coming to start it.
	if (!Player.IsPlaying() && !IsValid(Player.GetProvider()))
	{
		ScheduleQueue(&Player);
	}

	Record(EKzDialoguePlayResult::Queued, &Player);
	LastQueuedRequestId = Entry.RequestId;
	return &Player;
}

void UKzDialogueSubsystem::HandlePlayerFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (IsValid(Player))
	{
		FString Playing;
		PlayingRequests.RemoveAndCopyValue(Player, Playing);

		// Interrupted while a request is being admitted: that request took the channel.
		FString Outcome = UEnum::GetDisplayValueAsText(Reason).ToString();
		if (Reason == EKzDialogueFinishReason::Interrupted && !PendingRequest.Request.IsEmpty()) { Outcome += TEXT(" by ") + PendingRequest.Request; }
		AddHistory(Player->Channel, DescribeOwner(Player), Player->CurrentPriority, Playing, Outcome);
	}

	if (bDeinitializing || !IsValid(Player)) { return; }

	if (Queues.Contains(Player))
	{
		ScheduleQueue(Player);
	}

	// A concurrency slot of the channel freed up: other owners waiting for one may start.
	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(Player->Channel);
	if (ChannelDef && ChannelDef->MaxConcurrent > 0)
	{
		for (const TPair<TObjectPtr<UKzDialoguePlayer>, FKzDialogueChannelQueue>& Pair : Queues)
		{
			if (Pair.Key != Player && IsValid(Pair.Key) && UKzDialogueSettings::Get()->FindChannel(Pair.Key->Channel) == ChannelDef)
			{
				ScheduleQueue(Pair.Key);
			}
		}
	}
}

void UKzDialogueSubsystem::ScheduleQueue(UKzDialoguePlayer* Player)
{
	if (UWorld* World = GetWorld())
	{
		const TWeakObjectPtr<UKzDialoguePlayer> WeakPlayer = Player;
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]() { ProcessQueue(WeakPlayer.Get()); }));
	}
}

void UKzDialogueSubsystem::ProcessQueue(UKzDialoguePlayer* Player)
{
	if (!IsValid(Player)) { return; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(Player->Channel);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	while (!bDeinitializing)
	{
		// Something holds the player again: the queue waits for its next finish.
		if (Player->IsPlaying() || IsValid(Player->GetProvider())) { return; }

		// Found again each pass: the drop and start broadcasts may change the queues.
		FKzDialogueChannelQueue* Queue = Queues.Find(Player);
		if (!Queue || Queue->Entries.IsEmpty())
		{
			Queues.Remove(Player);
			return;
		}

		// Other owners fill the channel: the queue waits for one of them to finish.
		if (IsAtConcurrencyLimit(*Player, ChannelDef)) { return; }

		const FKzQueuedDialogue Entry = Queue->Entries[0];
		Queue->Entries.RemoveAt(0);

		const bool bExpired = ChannelDef && ChannelDef->QueueTimeout > 0.0f && Now - Entry.QueuedAt > ChannelDef->QueueTimeout;
		if (bExpired || !IsValid(Entry.Provider))
		{
			AddHistory(Player->Channel, DescribeOwner(Player), Entry.Priority, Entry.HistoryLabel, bExpired ? TEXT("Dropped: waited longer than the queue timeout") : TEXT("Dropped: its provider is gone"));
			OnQueuedRequestDropped.Broadcast(Player->Channel, Entry.RequestId);
			continue;
		}

		OnQueuedRequestStarted.Broadcast(Player->Channel, Entry.RequestId);
		if (StartOnPlayer(*Player, Entry.Provider, Entry.Priority, Entry.bInterruptible, Entry.AdvanceMode))
		{
			AddHistory(Player->Channel, DescribeOwner(Player), Entry.Priority, Entry.HistoryLabel, TEXT("Started from the queue"));
			PlayingRequests.Add(Player, Entry.HistoryLabel);
		}
		return;
	}
}

bool UKzDialogueSubsystem::CancelQueuedRequest(FGuid RequestId)
{
	for (TPair<TObjectPtr<UKzDialoguePlayer>, FKzDialogueChannelQueue>& Pair : Queues)
	{
		const int32 Index = Pair.Value.Entries.IndexOfByPredicate([&RequestId](const FKzQueuedDialogue& Entry) { return Entry.RequestId == RequestId; });
		if (Index != INDEX_NONE && IsValid(Pair.Key))
		{
			const FGameplayTag Channel = Pair.Key->Channel;
			AddHistory(Channel, DescribeOwner(Pair.Key), Pair.Value.Entries[Index].Priority, Pair.Value.Entries[Index].HistoryLabel, TEXT("Dropped: cancelled by its caller"));
			Pair.Value.Entries.RemoveAt(Index);
			OnQueuedRequestDropped.Broadcast(Channel, RequestId);
			return true;
		}
	}
	return false;
}

void UKzDialogueSubsystem::DropQueue(UKzDialoguePlayer* Player, const TCHAR* Cause)
{
	// Out of the map first, so a listener queueing again starts a fresh queue.
	FKzDialogueChannelQueue Dropped;
	if (!IsValid(Player) || !Queues.RemoveAndCopyValue(Player, Dropped)) { return; }

	for (const FKzQueuedDialogue& Entry : Dropped.Entries)
	{
		AddHistory(Player->Channel, DescribeOwner(Player), Entry.Priority, Entry.HistoryLabel, FString::Printf(TEXT("Dropped: %s"), Cause));
		OnQueuedRequestDropped.Broadcast(Player->Channel, Entry.RequestId);
	}
}

UKzDialoguePlayer* UKzDialogueSubsystem::StartOnPlayer(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode)
{
	if (Player.IsPlaying() || IsValid(Player.GetProvider()))
	{
		// A dialogue past its last line closes as it would have; anything else is interrupted.
		if (Player.GetState() == EKzDialogueState::Exiting) { Player.FinishExit(); }
		else { Player.Interrupt(); }
		if (Player.IsPlaying() || IsValid(Player.GetProvider()))
		{
			return nullptr;
		}
	}

	Player.CurrentPriority = Priority;
	Player.bCurrentInterruptible = bInterruptible;
	// Inherit at this level means "no asset to resolve against" (manual providers): fall to Automatic.
	Player.AdvanceMode = (AdvanceMode == EKzDialogueAdvanceMode::Inherit) ? EKzDialogueAdvanceMode::Automatic : AdvanceMode;
	Player.SetProvider(Provider);

	Player.StartDialogue();
	return &Player;
}

UKzDialoguePlayer* UKzDialogueSubsystem::StartOrEnqueue(UKzDialoguePlayer& Player, EKzDialoguePlayResult Admission, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef)
{
	if (Admission == EKzDialoguePlayResult::Queued)
	{
		return Enqueue(Player, Provider, Priority, bInterruptible, AdvanceMode, ChannelDef);
	}
	return Record(EKzDialoguePlayResult::Started, StartOnPlayer(Player, Provider, Priority, bInterruptible, AdvanceMode));
}

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode, bool bInterruptible, AActor* Owner)
{
	TGuardValue<FKzDialogueHistoryEntry> RequestGuard(PendingRequest, { 0.0, InChannel, Priority, IsValid(Provider) ? Provider->GetHistoryLabel() : FString(TEXT("(no provider)")), FString() });
	if (!IsValid(Provider))
	{
		return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr);
	}

	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);
	PendingRequest.Priority = ResolvedPriority;

	UKzDialoguePlayer* Player = GetPlayerForRequest(InChannel, Owner, nullptr);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, Player); }
	return StartOrEnqueue(*Player, Admission, Provider, ResolvedPriority, bInterruptible, AdvanceMode, ChannelDef);
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, EKzDialogueAdvanceMode AdvanceMode, AActor* Owner)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// The session splits the asset into maximal per-channel runs and plays each run on its own channel,
	// chaining them. A valid InChannel forces every line onto it -> one run, identical to the old play.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->Start(this, Asset, InChannel, AdvanceMode, Owner);
	return Session;
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode, AActor* Owner)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// Same session machinery as PlayAsset, over the caller's subset: per-entry channels, chained runs.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->StartEntries(this, Asset, LineIds, InChannel, Priority, AdvanceMode, Owner);
	return Session;
}

void UKzDialogueSubsystem::AddGraphInstance(UKzDialogueGraphInstance* Instance)
{
	GraphInstances.AddUnique(Instance);
}

void UKzDialogueSubsystem::RemoveGraphInstance(UKzDialogueGraphInstance* Instance)
{
	GraphInstances.RemoveSingle(Instance);
}

void UKzDialogueSubsystem::AddChoiceListener(UKzDialogueChoiceListener* Listener)
{
	// Those whose creator is gone go now; the rest notice it at the next start.
	ChoiceListeners.RemoveAll([](const UKzDialogueChoiceListener* Each) { return !IsValid(Each) || Each->IsAbandoned(); });
	ChoiceListeners.AddUnique(Listener);
}

void UKzDialogueSubsystem::RemoveChoiceListener(UKzDialogueChoiceListener* Listener)
{
	ChoiceListeners.RemoveSingle(Listener);
}

bool UKzDialogueSubsystem::ResolveLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine)
{
	return Ref.IsValid() && ResolveAssetEntry(Ref.Asset.LoadSynchronous(), Ref.LineId, OutLine);
}

void UKzDialogueSubsystem::ReleaseAssetSession(UKzDialogueAssetSession* Session)
{
	if (Session)
	{
		AssetSessions.RemoveSingle(Session);
	}
}

// Anti-spam: the same line is already showing through a manual provider.
static bool IsPlayingManualLine(const UKzDialoguePlayer& Player, const FGuid& LineId)
{
	const UKzManualDialogueProvider* ManualProv = Player.IsPlaying() ? Cast<UKzManualDialogueProvider>(Player.GetProvider()) : nullptr;
	if (!ManualProv || ManualProv->Line.LineId != LineId) { return false; }

	UE_LOG(LogKzDialogue, Verbose, TEXT("Channel '%s': line %s is already showing; the request joins it."), *Player.Channel.ToString(), *LineId.ToString());
	return true;
}

// Ad-hoc lines compare whole: one on an actor property shares its archetype's LineId with every instance.
static bool IsPlayingManualLine(const UKzDialoguePlayer& Player, const FKzDialogueLine& Line)
{
	const UKzManualDialogueProvider* ManualProv = Player.IsPlaying() ? Cast<UKzManualDialogueProvider>(Player.GetProvider()) : nullptr;
	return ManualProv && FKzDialogueLine::StaticStruct()->CompareScriptStruct(&ManualProv->Line, &Line, PPF_None) && IsPlayingManualLine(Player, Line.LineId);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority, AActor* Owner)
{
	// The line plays as given: its notify Timeline must already be resolved by the caller. Asset
	// paths (PlayAssetLine/-List, PlayLineRefs) do this via ResolveAssetEntry; an ad-hoc line has none.
	if (!InChannel.IsValid()) { InChannel = ResolveLineLevelChannel(Line); }
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	const FString Label = Line.SourceAsset.IsValid() ? DescribeEntry(Line.SourceAsset.Get(), Line.LineId) : FString::Printf(TEXT("line \"%s\""), *Line.Text.ToString().Left(48));
	TGuardValue<FKzDialogueHistoryEntry> RequestGuard(PendingRequest, { 0.0, InChannel, ResolvedPriority, Label, FString() });

	UKzDialoguePlayer* Player = GetPlayerForRequest(InChannel, Owner, Line.Speaker.Asset);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (IsPlayingManualLine(*Player, Line)) { return Record(EKzDialoguePlayResult::Joined, Player); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission == EKzDialoguePlayResult::Queued)
	{
		return Enqueue(*Player, UKzManualDialogueProvider::Create(this, Line), ResolvedPriority, /*bInterruptible=*/true, EKzDialogueAdvanceMode::Automatic, ChannelDef);
	}
	if (Admission != EKzDialoguePlayResult::Started) { return Record(Admission, Player); }
	return Record(EKzDialoguePlayResult::Started, CommitLine(*Player, Line, ResolvedPriority, /*bInterruptible=*/true));
}

UKzDialoguePlayer* UKzDialogueSubsystem::CommitLine(UKzDialoguePlayer& Player, const FKzDialogueLine& Line, int32 Priority, bool bInterruptible)
{
	if (IsPlayingManualLine(Player, Line)) { return &Player; }

	// Hot-swap inside the manual provider: a line fade instead of a full exit and enter (Sequencer lines back to back).
	UKzManualDialogueProvider* ManualProv = Player.IsPlaying() ? Cast<UKzManualDialogueProvider>(Player.GetProvider()) : nullptr;
	if (ManualProv && Player.GetState() != EKzDialogueState::Exiting)
	{
		if (const FString* Playing = PlayingRequests.Find(&Player))
		{
			AddHistory(Player.Channel, DescribeOwner(&Player), Player.CurrentPriority, *Playing, TEXT("Replaced by ") + PendingRequest.Request);
		}
		Player.CurrentPriority = Priority;
		Player.bCurrentInterruptible = bInterruptible;
		ManualProv->SetLine(Line);
		Player.Next();
		return &Player;
	}

	return StartOnPlayer(Player, UKzManualDialogueProvider::Create(this, Line), Priority, bInterruptible, EKzDialogueAdvanceMode::Automatic);
}

bool UKzDialogueSubsystem::ResolveAssetEntry(UKzDialogueAsset* Asset, const FGuid& LineId, FKzDialogueLine& OutLine)
{
	if (!IsValid(Asset) || !LineId.IsValid()) { return false; }

	FGuid LineIdToPlay = LineId;

	if (const FKzDialogueAlias* Alias = Asset->FindAlias(LineId))
	{
		LineIdToPlay = ResolveAliasInternal(*Alias);
		if (!LineIdToPlay.IsValid())
		{
			return false;
		}
	}

	return Asset->TryGetLineById(LineIdToPlay, OutLine);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority, AActor* Owner)
{
	TGuardValue<FKzDialogueHistoryEntry> RequestGuard(PendingRequest, { 0.0, InChannel, Priority, DescribeEntry(Asset, LineId), FString() });
	if (!IsValid(Asset) || !LineId.IsValid()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// The authored entry, not the alias pick, so callers resolving ahead (async actions) land on the same player.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineId);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);
	PendingRequest.Priority = ResolvedPriority;

	UKzDialoguePlayer* Player = GetPlayerForRequest(InChannel, Owner, Asset->GetEntrySpeaker(LineId));
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (IsPlayingManualLine(*Player, LineId)) { return Record(EKzDialoguePlayResult::Joined, Player); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, Player); }

	// Aliases resolve only once admitted or queued, so a refused request leaves their cooldown and shuffle bag untouched.
	FKzDialogueLine Line;
	if (!ResolveAssetEntry(Asset, LineId, Line)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (Admission == EKzDialoguePlayResult::Queued)
	{
		return Enqueue(*Player, UKzManualDialogueProvider::Create(this, Line), ResolvedPriority, Asset->bInterruptible, EKzDialogueAdvanceMode::Automatic, ChannelDef);
	}
	return Record(EKzDialoguePlayResult::Started, CommitLine(*Player, Line, ResolvedPriority, Asset->bInterruptible));
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode, AActor* Owner)
{
	TGuardValue<FKzDialogueHistoryEntry> RequestGuard(PendingRequest, { 0.0, InChannel, Priority, DescribeEntry(Asset, LineIds.IsEmpty() ? FGuid() : LineIds[0], LineIds.Num()), FString() });
	if (!IsValid(Asset) || LineIds.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// First-entry rule: the channel and the owner resolve from the first AUTHORED entry, so callers
	// resolving ahead stay deterministic even when entries get skipped.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineIds[0]);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);
	PendingRequest.Priority = ResolvedPriority;

	UKzDialoguePlayer* Player = GetPlayerForRequest(InChannel, Owner, Asset->GetEntrySpeaker(LineIds[0]));
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, Player); }

	TArray<FKzDialogueLine> Lines;
	Lines.Reserve(LineIds.Num());

	for (const FGuid& LineId : LineIds)
	{
		FKzDialogueLine Line;
		if (ResolveAssetEntry(Asset, LineId, Line))
		{
			Lines.Add(MoveTemp(Line));
		}
		else
		{
			UE_LOG(LogKzDialogue, Warning, TEXT("PlayAssetLineList: id '%s' does not resolve in '%s'; skipping."), *LineId.ToString(), *Asset->GetName());
		}
	}

	if (Lines.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	return StartOrEnqueue(*Player, Admission, UKzLineListDialogueProvider::Create(this, Lines), ResolvedPriority, Asset->bInterruptible, AdvanceMode, ChannelDef);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority, AActor* Owner)
{
	TGuardValue<FKzDialogueHistoryEntry> RequestGuard(PendingRequest, { 0.0, InChannel, Priority, FString(TEXT("(no refs)")), FString() });
	if (Refs.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// First-entry rule, on the first AUTHORED ref (see PlayAssetLineList).
	const UKzDialogueAsset* FirstAsset = Refs[0].Asset.LoadSynchronous();
	InChannel = ResolveChannelForEntry(InChannel, FirstAsset, Refs[0].LineId);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, FirstAsset ? FirstAsset->Priority : InheritPriority, ChannelDef);
	PendingRequest = { 0.0, InChannel, ResolvedPriority, DescribeEntry(FirstAsset, Refs[0].LineId, Refs.Num()), FString() };

	UKzDialoguePlayer* Player = GetPlayerForRequest(InChannel, Owner, FirstAsset ? FirstAsset->GetEntrySpeaker(Refs[0].LineId) : nullptr);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, Player); }

	TArray<FKzDialogueLine> Lines;
	Lines.Reserve(Refs.Num());

	for (const FKzDialogueLineRef& Ref : Refs)
	{
		if (!Ref.IsValid()) { continue; }

		UKzDialogueAsset* Asset = Ref.Asset.LoadSynchronous();

		FKzDialogueLine Line;
		if (ResolveAssetEntry(Asset, Ref.LineId, Line))
		{
			Lines.Add(MoveTemp(Line));
		}
		else
		{
			UE_LOG(LogKzDialogue, Warning, TEXT("PlayLineRefs: ref '%s' does not resolve; skipping."), *Ref.Asset.ToString());
		}
	}

	if (Lines.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	return StartOrEnqueue(*Player, Admission, UKzLineListDialogueProvider::Create(this, Lines), ResolvedPriority, !FirstAsset || FirstAsset->bInterruptible, EKzDialogueAdvanceMode::Automatic, ChannelDef);
}

void UKzDialogueSubsystem::StopChannel(FGameplayTag InChannel, AActor* Owner)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
		if (Owner && Player->Owner != ResolveOwner(Player->Channel, Owner)) { continue; }

		// Silencing a channel covers what was waiting for it too; dropped first, so the finish starts nothing.
		DropQueue(Player, TEXT("the channel was stopped"));
		Player->Stop();
	}
}

void UKzDialogueSubsystem::StopAll()
{
	// Snapshot: with no view claiming acks, Stop() finishes synchronously and a finished
	// handler may start a new dialogue, growing Players mid-loop. Players created by those
	// handlers are deliberately not stopped.
	TArray<UKzDialoguePlayer*> Snapshot;
	GetAllPlayers(Snapshot);
	for (UKzDialoguePlayer* Player : Snapshot)
	{
		DropQueue(Player, TEXT("the channel was stopped"));
		Player->Stop();
	}
}

void UKzDialogueSubsystem::InterruptChannel(FGameplayTag InChannel, AActor* Owner)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
		if (Owner && Player->Owner != ResolveOwner(Player->Channel, Owner)) { continue; }

		DropQueue(Player, TEXT("the channel was interrupted"));
		Player->Interrupt();
	}
}

void UKzDialogueSubsystem::InterruptAll()
{
	// Same snapshot as StopAll: Interrupt() always finishes synchronously.
	TArray<UKzDialoguePlayer*> Snapshot;
	GetAllPlayers(Snapshot);
	for (UKzDialoguePlayer* Player : Snapshot)
	{
		DropQueue(Player, TEXT("the channel was interrupted"));
		Player->Interrupt();
	}
}

void UKzDialogueSubsystem::GetAllPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const
{
	OutPlayers.Reset();
	OutPlayers.Reserve(Players.Num());
	for (UKzDialoguePlayer* Player : Players)
	{
		if (IsValid(Player)) { OutPlayers.Add(Player); }
	}
}

void UKzDialogueSubsystem::ResetAliasState(FGuid AliasId)
{
	AliasCooldownStarts.Remove(AliasId);
	if (UKzDialogueMemorySubsystem* Memory = UKzDialogueMemorySubsystem::Get(this)) { Memory->ForgetAlias(AliasId); }
}

void UKzDialogueSubsystem::ResetAllAliasStates()
{
	AliasCooldownStarts.Reset();
	if (UKzDialogueMemorySubsystem* Memory = UKzDialogueMemorySubsystem::Get(this)) { Memory->ForgetAllAliases(); }
}


FGuid UKzDialogueSubsystem::ResolveAliasInternal(const FKzDialogueAlias& Alias)
{
	if (Alias.LineIds.Num() == 0) { return FGuid(); }

	// The picks outlive levels and go into saves; without a game instance (editor previews) each pick starts fresh.
	FKzAliasMemory Scratch;
	UKzDialogueMemorySubsystem* Memory = UKzDialogueMemorySubsystem::Get(this);
	FKzAliasMemory& State = Memory ? Memory->FindOrAddAliasMemory(Alias.AliasId) : Scratch;

	// World time, so pause and dilation stretch the cooldown too.
	const double* LastResolved = AliasCooldownStarts.Find(Alias.AliasId);
	if (Alias.CooldownSeconds > 0.0f && LastResolved)
	{
		const UWorld* World = GetWorld();
		if (World && World->TimeSince(*LastResolved) < Alias.CooldownSeconds)
		{
			UE_LOG(LogKzDialogue, Verbose, TEXT("Alias '%s' is cooling down (%.1f s left); nothing plays."), *Alias.AliasName.ToString(), Alias.CooldownSeconds - World->TimeSince(*LastResolved));
			return FGuid();
		}
	}

	auto MarkResolved = [this, &State, &Alias](const FGuid& Picked) -> FGuid
	{
		State.LastPickedLineId = Picked;
		if (const UWorld* World = GetWorld())
		{
			AliasCooldownStarts.Add(Alias.AliasId, World->GetTimeSeconds());
		}
		return Picked;
	};

	if (Alias.LineIds.Num() == 1) { return MarkResolved(Alias.LineIds[0]); }

	auto PickRandom = [](const TArray<FGuid>& Candidates) -> FGuid
	{
		const int32 Index = FMath::RandRange(0, Candidates.Num() - 1);
		return Candidates[Index];
	};

	switch (Alias.SelectionMode)
	{
		case EKzAliasSelectionMode::Random:
		{
			return MarkResolved(PickRandom(Alias.LineIds));
		}

		case EKzAliasSelectionMode::RandomNoRepeat:
		{
			if (!State.LastPickedLineId.IsValid())
			{
				return MarkResolved(PickRandom(Alias.LineIds));
			}

			TArray<FGuid> Candidates;
			Candidates.Reserve(Alias.LineIds.Num() - 1);
			for (const FGuid& Id : Alias.LineIds)
			{
				if (Id != State.LastPickedLineId)
				{
					Candidates.Add(Id);
				}
			}
			if (Candidates.Num() == 0)
			{
				// Edge case: every line equals LastPickedLineId (shouldn't happen with
				// EnsureLineGuids dedup, but guard anyway).
				return MarkResolved(Alias.LineIds[0]);
			}

			return MarkResolved(PickRandom(Candidates));
		}

		case EKzAliasSelectionMode::ShuffleBag:
		{
			// Rebuild the bag if needed: empty, drained, or alias's LineIds changed
			// (asset edited mid-session).
			const bool bBagDrained = State.ShuffleCursor >= State.ShuffleBag.Num();
			const bool bSourceChanged = State.ShuffleBagSourceIds != Alias.LineIds;
			const bool bBagNeedsRebuild = State.ShuffleBag.Num() == 0 || bBagDrained || bSourceChanged;

			if (bBagNeedsRebuild)
			{
				State.ShuffleBag = Alias.LineIds;
				State.ShuffleBagSourceIds = Alias.LineIds;
				State.ShuffleCursor = 0;

				Algo::RandomShuffle(State.ShuffleBag);

				// Avoid back-to-back repeat across bag boundary: if the new bag's first
				// entry equals the last line we played, swap it with another entry.
				// With N >= 2 this is always possible.
				if (State.LastPickedLineId.IsValid() && State.ShuffleBag.Num() >= 2 && State.ShuffleBag[0] == State.LastPickedLineId)
				{
					const int32 SwapWith = FMath::RandRange(1, State.ShuffleBag.Num() - 1);
					State.ShuffleBag.Swap(0, SwapWith);
				}
			}

			const FGuid Picked = State.ShuffleBag[State.ShuffleCursor];
			++State.ShuffleCursor;
			return MarkResolved(Picked);
		}

		case EKzAliasSelectionMode::Sequential:
		{
			// If the alias's LineIds changed, clamp the cursor.
			if (State.SequentialCursor >= Alias.LineIds.Num())
			{
				State.SequentialCursor = 0;
			}

			const FGuid Picked = Alias.LineIds[State.SequentialCursor];
			State.SequentialCursor = (State.SequentialCursor + 1) % Alias.LineIds.Num();
			return MarkResolved(Picked);
		}
	}

	return FGuid();
}