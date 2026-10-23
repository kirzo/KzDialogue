// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialogue.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "Settings/KzDialogueSettings.h"
#include "KzDialogueMemorySubsystem.h"
#include "KzSpeakerAsset.h"
#include "Engine/World.h"
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
}

void UKzDialogueSubsystem::Deinitialize()
{
	bDeinitializing = true;

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
	Super::Deinitialize();
}

UKzDialoguePlayer* UKzDialogueSubsystem::GetOrCreatePlayer(FGameplayTag InChannel)
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	if (TObjectPtr<UKzDialoguePlayer>* Existing = Players.Find(InChannel))
	{
		return Existing->Get();
	}

	if (!UKzDialogueSettings::Get()->FindChannel(InChannel))
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Channel '%s' has no definition for it or any ancestor in Project Settings -> Plugins -> KzDialogue -> Channels; using fallback values."), *InChannel.ToString());
	}

	UKzDialoguePlayer* NewPlayer = NewObject<UKzDialoguePlayer>(this);
	NewPlayer->Channel = InChannel;
	NewPlayer->OnDialogueFinished.AddDynamic(this, &UKzDialogueSubsystem::HandlePlayerFinished);
	Players.Add(InChannel, NewPlayer);

	// Broadcast BEFORE anything plays on it, so views can bind and subscribe to its request events
	// ahead of the first StartDialogue.
	OnPlayerCreated.Broadcast(InChannel, NewPlayer);

	return NewPlayer;
}

void UKzDialogueSubsystem::GetPlayersInScope(FGameplayTag Scope, TArray<UKzDialoguePlayer*>& OutPlayers) const
{
	OutPlayers.Reset();
	if (!Scope.IsValid()) { Scope = DefaultChannel; }

	for (const auto& Pair : Players)
	{
		if (IsValid(Pair.Value) && Pair.Key.MatchesTag(Scope))
		{
			OutPlayers.Add(Pair.Value);
		}
	}
}

bool UKzDialogueSubsystem::IsSpeakerTalking(const UKzSpeakerAsset* Speaker, FGameplayTag ExceptChannel) const
{
	if (!Speaker) { return false; }

	for (const auto& Pair : Players)
	{
		const UKzDialoguePlayer* Player = Pair.Value;
		if (Pair.Key != ExceptChannel && IsValid(Player) && Player->IsPlaying() && Player->GetCurrentLine().Speaker.Asset == Speaker)
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

UKzDialoguePlayer* UKzDialogueSubsystem::FindPlayer(FGameplayTag InChannel) const
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }
	if (const TObjectPtr<UKzDialoguePlayer>* Found = Players.Find(InChannel))
	{
		return Found->Get();
	}
	return nullptr;
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

EKzDialoguePlayResult UKzDialogueSubsystem::Admit(const UKzDialoguePlayer& Player, int32 Priority, const FKzDialogueChannelDefinition* ChannelDef) const
{
	const FKzDialogueChannelQueue* Queue = Queues.Find(Player.Channel);
	const bool bOthersWaiting = Queue && Queue->Entries.Num() > 0;

	EKzDialoguePlayResult Result = EKzDialoguePlayResult::Started;
	if (bDeinitializing)
	{
		Result = EKzDialoguePlayResult::RejectedTearingDown;
	}
	else if ((!Player.IsPlaying() && !IsValid(Player.GetProvider())) || Player.GetState() == EKzDialogueState::Exiting)
	{
		// Free, or only its exit animations are left; requests already waiting still go first.
		if (!bOthersWaiting) { return EKzDialoguePlayResult::Started; }
		Result = EKzDialoguePlayResult::Queued;
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
	if (Result != EKzDialoguePlayResult::RejectedTearingDown && ChannelDef && ChannelDef->BusyPolicy != EKzChannelBusyPolicy::Reject)
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

UKzDialoguePlayer* UKzDialogueSubsystem::Record(EKzDialoguePlayResult Result, UKzDialoguePlayer* Player)
{
	// A start that came back empty: a listener of the ending took the channel first.
	if (Result == EKzDialoguePlayResult::Started && !Player) { Result = EKzDialoguePlayResult::RejectedPriority; }

	LastPlayResult = Result;
	LastQueuedRequestId.Invalidate();
	const bool bPlays = Result == EKzDialoguePlayResult::Started || Result == EKzDialoguePlayResult::Joined || Result == EKzDialoguePlayResult::Queued;
	return bPlays ? Player : nullptr;
}

UKzDialoguePlayer* UKzDialogueSubsystem::Enqueue(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef)
{
	// Replace keeps only the newest request; what was waiting is reported dropped before the queue changes again.
	if (ChannelDef && ChannelDef->BusyPolicy == EKzChannelBusyPolicy::Replace)
	{
		DropQueue(Player.Channel);
	}

	FKzQueuedDialogue Entry;
	Entry.Provider = Provider;
	Entry.RequestId = FGuid::NewGuid();
	Entry.Priority = Priority;
	Entry.bInterruptible = bInterruptible;
	Entry.AdvanceMode = AdvanceMode;
	Entry.QueuedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// Higher priority first; equal priorities keep arrival order.
	TArray<FKzQueuedDialogue>& Entries = Queues.FindOrAdd(Player.Channel).Entries;
	const int32 Index = Entries.IndexOfByPredicate([Priority](const FKzQueuedDialogue& Waiting) { return Waiting.Priority < Priority; });
	Entries.Insert(Entry, Index == INDEX_NONE ? Entries.Num() : Index);
	UE_LOG(LogKzDialogue, Verbose, TEXT("Channel '%s' queued a priority %d request (%d waiting)."), *Player.Channel.ToString(), Priority, Entries.Num());

	// A free channel has no finish coming to start it.
	if (!Player.IsPlaying() && !IsValid(Player.GetProvider()))
	{
		ScheduleQueue(Player.Channel);
	}

	Record(EKzDialoguePlayResult::Queued, &Player);
	LastQueuedRequestId = Entry.RequestId;
	return &Player;
}

void UKzDialogueSubsystem::HandlePlayerFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (!bDeinitializing && IsValid(Player) && Queues.Contains(Player->Channel))
	{
		ScheduleQueue(Player->Channel);
	}
}

void UKzDialogueSubsystem::ScheduleQueue(FGameplayTag Channel)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Channel]() { ProcessQueue(Channel); }));
	}
}

void UKzDialogueSubsystem::ProcessQueue(FGameplayTag Channel)
{
	UKzDialoguePlayer* Player = FindPlayer(Channel);
	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(Channel);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	while (!bDeinitializing && IsValid(Player))
	{
		// Something holds the channel again: the queue waits for its next finish.
		if (Player->IsPlaying() || IsValid(Player->GetProvider())) { return; }

		// Found again each pass: the drop and start broadcasts may change the queues.
		FKzDialogueChannelQueue* Queue = Queues.Find(Channel);
		if (!Queue || Queue->Entries.IsEmpty())
		{
			Queues.Remove(Channel);
			return;
		}

		const FKzQueuedDialogue Entry = Queue->Entries[0];
		Queue->Entries.RemoveAt(0);

		const bool bExpired = ChannelDef && ChannelDef->QueueTimeout > 0.0f && Now - Entry.QueuedAt > ChannelDef->QueueTimeout;
		if (bExpired || !IsValid(Entry.Provider))
		{
			OnQueuedRequestDropped.Broadcast(Channel, Entry.RequestId);
			continue;
		}

		OnQueuedRequestStarted.Broadcast(Channel, Entry.RequestId);
		StartOnPlayer(*Player, Entry.Provider, Entry.Priority, Entry.bInterruptible, Entry.AdvanceMode);
		return;
	}
}

bool UKzDialogueSubsystem::CancelQueuedRequest(FGuid RequestId)
{
	for (TPair<FGameplayTag, FKzDialogueChannelQueue>& Pair : Queues)
	{
		const int32 Index = Pair.Value.Entries.IndexOfByPredicate([&RequestId](const FKzQueuedDialogue& Entry) { return Entry.RequestId == RequestId; });
		if (Index != INDEX_NONE)
		{
			const FGameplayTag Channel = Pair.Key;
			Pair.Value.Entries.RemoveAt(Index);
			OnQueuedRequestDropped.Broadcast(Channel, RequestId);
			return true;
		}
	}
	return false;
}

void UKzDialogueSubsystem::DropQueue(FGameplayTag Channel)
{
	// Out of the map first, so a listener queueing again starts a fresh queue.
	FKzDialogueChannelQueue Dropped;
	if (!Queues.RemoveAndCopyValue(Channel, Dropped)) { return; }

	for (const FKzQueuedDialogue& Entry : Dropped.Entries)
	{
		OnQueuedRequestDropped.Broadcast(Channel, Entry.RequestId);
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

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Provider))
	{
		return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr);
	}

	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, nullptr); }
	return StartOrEnqueue(*Player, Admission, Provider, ResolvedPriority, /*bInterruptible=*/true, AdvanceMode, ChannelDef);
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// The session splits the asset into maximal per-channel runs and plays each run on its own channel,
	// chaining them. A valid InChannel forces every line onto it -> one run, identical to the old play.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->Start(this, Asset, InChannel, AdvanceMode);
	return Session;
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// Same session machinery as PlayAsset, over the caller's subset: per-entry channels, chained runs.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->StartEntries(this, Asset, LineIds, InChannel, Priority, AdvanceMode);
	return Session;
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

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority)
{
	// The line plays as given: its notify Timeline must already be resolved by the caller. Asset
	// paths (PlayAssetLine/-List, PlayLineRefs) do this via ResolveAssetEntry; an ad-hoc line has none.
	if (!InChannel.IsValid()) { InChannel = ResolveLineLevelChannel(Line); }
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (IsPlayingManualLine(*Player, Line)) { return Record(EKzDialoguePlayResult::Joined, Player); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission == EKzDialoguePlayResult::Queued)
	{
		return Enqueue(*Player, UKzManualDialogueProvider::Create(this, Line), ResolvedPriority, /*bInterruptible=*/true, EKzDialogueAdvanceMode::Automatic, ChannelDef);
	}
	if (Admission != EKzDialoguePlayResult::Started) { return Record(Admission, nullptr); }
	return Record(EKzDialoguePlayResult::Started, CommitLine(*Player, Line, ResolvedPriority, /*bInterruptible=*/true));
}

UKzDialoguePlayer* UKzDialogueSubsystem::CommitLine(UKzDialoguePlayer& Player, const FKzDialogueLine& Line, int32 Priority, bool bInterruptible)
{
	if (IsPlayingManualLine(Player, Line)) { return &Player; }

	// Hot-swap inside the manual provider: a line fade instead of a full exit and enter (Sequencer lines back to back).
	UKzManualDialogueProvider* ManualProv = Player.IsPlaying() ? Cast<UKzManualDialogueProvider>(Player.GetProvider()) : nullptr;
	if (ManualProv && Player.GetState() != EKzDialogueState::Exiting)
	{
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

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority)
{
	if (!IsValid(Asset) || !LineId.IsValid()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// The authored entry, not the alias pick, so callers resolving ahead (async actions) land on the same player.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineId);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (IsPlayingManualLine(*Player, LineId)) { return Record(EKzDialoguePlayResult::Joined, Player); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, nullptr); }

	// Aliases resolve only once admitted or queued, so a refused request leaves their cooldown and shuffle bag untouched.
	FKzDialogueLine Line;
	if (!ResolveAssetEntry(Asset, LineId, Line)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	if (Admission == EKzDialoguePlayResult::Queued)
	{
		return Enqueue(*Player, UKzManualDialogueProvider::Create(this, Line), ResolvedPriority, Asset->bInterruptible, EKzDialogueAdvanceMode::Automatic, ChannelDef);
	}
	return Record(EKzDialoguePlayResult::Started, CommitLine(*Player, Line, ResolvedPriority, Asset->bInterruptible));
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset) || LineIds.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// First-entry rule: the channel resolves from the first AUTHORED entry, so callers resolving
	// ahead stay deterministic even when entries get skipped.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineIds[0]);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, nullptr); }

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

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority)
{
	if (Refs.IsEmpty()) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	// First-entry rule, on the first AUTHORED ref (see PlayAssetLineList).
	const UKzDialogueAsset* FirstAsset = Refs[0].Asset.LoadSynchronous();
	InChannel = ResolveChannelForEntry(InChannel, FirstAsset, Refs[0].LineId);

	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, FirstAsset ? FirstAsset->Priority : InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return Record(EKzDialoguePlayResult::RejectedNothingToPlay, nullptr); }

	const EKzDialoguePlayResult Admission = Admit(*Player, ResolvedPriority, ChannelDef);
	if (Admission != EKzDialoguePlayResult::Started && Admission != EKzDialoguePlayResult::Queued) { return Record(Admission, nullptr); }

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

void UKzDialogueSubsystem::StopChannel(FGameplayTag InChannel)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
		// Silencing a channel covers what was waiting for it too; dropped first, so the finish starts nothing.
		DropQueue(Player->Channel);
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
		DropQueue(Player->Channel);
		Player->Stop();
	}
}

void UKzDialogueSubsystem::InterruptChannel(FGameplayTag InChannel)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
		DropQueue(Player->Channel);
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
		DropQueue(Player->Channel);
		Player->Interrupt();
	}
}

void UKzDialogueSubsystem::GetAllPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const
{
	OutPlayers.Reset();
	OutPlayers.Reserve(Players.Num());
	for (const auto& Pair : Players)
	{
		if (IsValid(Pair.Value)) { OutPlayers.Add(Pair.Value); }
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