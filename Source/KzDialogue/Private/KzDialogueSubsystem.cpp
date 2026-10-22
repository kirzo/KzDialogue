// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialogue.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "Settings/KzDialogueSettings.h"
#include "Engine/World.h"
#include "Algo/RandomShuffle.h"
#include "Sound/SoundBase.h"

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

	UKzDialoguePlayer* NewPlayer = NewObject<UKzDialoguePlayer>(this);
	NewPlayer->Channel = InChannel;
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

// Line-level resolution: the line's own channel, else the channel mapped to its audio's
// SoundClass in project settings. Empty when neither applies — the chain moves to the asset.
// The SoundClass step is skipped when the line has no audio, the audio fails to load or has
// no SoundClass, or settings don't map it (invalid mappings also count as unmapped).
static FGameplayTag ResolveLineLevelChannel(const FKzDialogueLine& Line)
{
	if (Line.DefaultChannel.IsValid()) { return Line.DefaultChannel; }

	if (!Line.Audio.IsNull())
	{
		if (const USoundBase* Sound = Line.Audio.LoadSynchronous())
		{
			if (const UKzDialogueSettings* Settings = UKzDialogueSettings::Get())
			{
				return Settings->FindChannelForSoundClass(Sound->GetSoundClass());
			}
		}
	}

	return FGameplayTag();
}

FGameplayTag UKzDialogueSubsystem::ResolveChannel(FGameplayTag ExplicitChannel, const FKzDialogueLine& Line, const UKzDialogueAsset* Asset) const
{
	if (ExplicitChannel.IsValid()) { return ExplicitChannel; }

	const FGameplayTag LineLevel = ResolveLineLevelChannel(Line);
	if (LineLevel.IsValid()) { return LineLevel; }

	if (IsValid(Asset) && Asset->DefaultChannel.IsValid()) { return Asset->DefaultChannel; }
	return DefaultChannel;
}

// Returns the channel shared by every resolvable line of the alias, or empty when they disagree.
static FGameplayTag GetAliasUnanimousChannel(const UKzDialogueAsset& Asset, const FKzDialogueAlias& Alias)
{
	FGameplayTag Unanimous;
	bool bFirst = true;

	for (const FGuid& LineId : Alias.LineIds)
	{
		FKzDialogueLine Line;
		if (!Asset.TryGetLineById(LineId, Line)) { continue; }

		if (bFirst)
		{
			Unanimous = Line.DefaultChannel;
			bFirst = false;
		}
		else if (Line.DefaultChannel != Unanimous)
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
		FKzDialogueLine Line;
		if (Asset->TryGetLineById(EntryId, Line))
		{
			const FGameplayTag LineLevel = ResolveLineLevelChannel(Line);
			if (LineLevel.IsValid())
			{
				return LineLevel;
			}
		}

		// Alias entry: the alias's own channel, else its lines' unanimous one.
		FKzDialogueAlias Alias;
		if (Asset->TryGetAliasById(EntryId, Alias))
		{
			if (Alias.DefaultChannel.IsValid())
			{
				return Alias.DefaultChannel;
			}

			const FGameplayTag Unanimous = GetAliasUnanimousChannel(*Asset, Alias);
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

const FKzDialogueChannelDefinition* UKzDialogueSubsystem::FindChannelDefinition(const FGameplayTag& Tag) const
{
	if (const UKzDialogueSettings* Settings = UKzDialogueSettings::Get())
	{
		// Walk up the hierarchy: Dialogue.Channel.Bark.MyCharacter inherits the definition
		// declared for Dialogue.Channel.Bark, so per-character channels need no declaration.
		for (FGameplayTag Cursor = Tag; Cursor.IsValid(); Cursor = Cursor.RequestDirectParent())
		{
			if (const FKzDialogueChannelDefinition* Found = Settings->FindChannel(Cursor))
			{
				return Found;
			}
		}

		if (Tag.IsValid())
		{
			UE_LOG(LogKzDialogue, Warning,
				TEXT("Playing on channel '%s' with no definition for it or any ancestor in KzDialogueSettings. "
					"Using fallback values. Consider adding it under Project Settings -> Plugins -> KzDialogue -> Channels."),
				*Tag.ToString());
		}
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

UKzDialogueSubsystem::EKzPlayAdmission UKzDialogueSubsystem::Admit(const UKzDialoguePlayer& Player, int32 Priority, const FKzDialogueChannelDefinition* ChannelDef) const
{
	EKzPlayAdmission Result = EKzPlayAdmission::Admitted;
	if (bDeinitializing)
	{
		Result = EKzPlayAdmission::RejectedDeinitializing;
	}
	else if (!Player.IsPlaying() && !IsValid(Player.GetProvider()))
	{
		return EKzPlayAdmission::Admitted;
	}
	else if (Player.GetState() == EKzDialogueState::Exiting)
	{
		// Only its exit animations are left: the channel is as good as free.
		return EKzPlayAdmission::Admitted;
	}
	else if (Player.CurrentPriority > Priority)
	{
		Result = EKzPlayAdmission::RejectedPriority;
	}
	else if ((ChannelDef && !ChannelDef->bAllowInterruption) || !Player.bCurrentInterruptible)
	{
		Result = EKzPlayAdmission::RejectedNotInterruptible;
	}

	if (Result != EKzPlayAdmission::Admitted)
	{
		static const TCHAR* Reasons[] = { TEXT("admitted"), TEXT("a higher priority holds it"), TEXT("the playing dialogue is not interruptible"), TEXT("the world is tearing down") };
		UE_LOG(LogKzDialogue, Verbose, TEXT("Channel '%s' refused a priority %d request: %s."), *Player.Channel.ToString(), Priority, Reasons[static_cast<int32>(Result)]);
	}
	return Result;
}

UKzDialoguePlayer* UKzDialogueSubsystem::StartOnPlayer(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (Player.IsPlaying() || IsValid(Player.GetProvider()))
	{
		Player.Interrupt();
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

	if (bStartImmediately)
	{
		Player.StartDialogue();
	}
	return &Player;
}

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Provider))
	{
		return nullptr;
	}

	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player) || Admit(*Player, ResolvedPriority, ChannelDef) != EKzPlayAdmission::Admitted)
	{
		return nullptr;
	}
	return StartOnPlayer(*Player, Provider, ResolvedPriority, /*bInterruptible=*/true, bStartImmediately, AdvanceMode);
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// The session splits the asset into maximal per-channel runs and plays each run on its own channel,
	// chaining them. A valid InChannel forces every line onto it -> one run, identical to the old play.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->Start(this, Asset, InChannel, bStartImmediately, AdvanceMode);
	return Session;
}

UKzDialogueAssetSession* UKzDialogueSubsystem::PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	// Same session machinery as PlayAsset, over the caller's subset: per-entry channels, chained runs.
	UKzDialogueAssetSession* Session = NewObject<UKzDialogueAssetSession>(this);
	AssetSessions.Add(Session);
	Session->StartEntries(this, Asset, LineIds, InChannel, Priority, bStartImmediately, AdvanceMode);
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
	return ManualProv && ManualProv->Line.LineId == LineId;
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	// The line plays as given: its notify Timeline must already be resolved by the caller. Asset
	// paths (PlayAssetLine/-List, PlayLineRefs) do this via ResolveAssetEntry; an ad-hoc line has none.
	InChannel = ResolveChannel(InChannel, Line, /*Asset*/ nullptr);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return nullptr; }

	if (IsPlayingManualLine(*Player, Line.LineId)) { return Player; }
	if (Admit(*Player, ResolvedPriority, ChannelDef) != EKzPlayAdmission::Admitted) { return nullptr; }
	return CommitLine(*Player, Line, ResolvedPriority, /*bInterruptible=*/true, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::CommitLine(UKzDialoguePlayer& Player, const FKzDialogueLine& Line, int32 Priority, bool bInterruptible, bool bStartImmediately)
{
	if (IsPlayingManualLine(Player, Line.LineId)) { return &Player; }

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

	return StartOnPlayer(Player, UKzManualDialogueProvider::Create(this, Line), Priority, bInterruptible, bStartImmediately, EKzDialogueAdvanceMode::Automatic);
}

bool UKzDialogueSubsystem::ResolveAssetEntry(UKzDialogueAsset* Asset, const FGuid& LineId, FKzDialogueLine& OutLine)
{
	if (!IsValid(Asset) || !LineId.IsValid()) { return false; }

	FGuid LineIdToPlay = LineId;

	FKzDialogueAlias Alias;
	if (Asset->TryGetAliasById(LineId, Alias))
	{
		LineIdToPlay = ResolveAliasInternal(Alias);
		if (!LineIdToPlay.IsValid())
		{
			return false;
		}
	}

	return Asset->TryGetLineById(LineIdToPlay, OutLine);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	if (!IsValid(Asset) || !LineId.IsValid()) { return nullptr; }

	// The authored entry, not the alias pick, so callers resolving ahead (async actions) land on the same player.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineId);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) { return nullptr; }

	if (IsPlayingManualLine(*Player, LineId)) { return Player; }
	if (Admit(*Player, ResolvedPriority, ChannelDef) != EKzPlayAdmission::Admitted) { return nullptr; }

	// Aliases resolve only once admitted, so a refused request leaves their cooldown and shuffle bag untouched.
	FKzDialogueLine Line;
	if (!ResolveAssetEntry(Asset, LineId, Line)) { return nullptr; }
	return CommitLine(*Player, Line, ResolvedPriority, Asset->bInterruptible, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset) || LineIds.IsEmpty()) { return nullptr; }

	// First-entry rule: the channel resolves from the first AUTHORED entry, so callers resolving
	// ahead stay deterministic even when entries get skipped.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineIds[0]);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, Asset->Priority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player) || Admit(*Player, ResolvedPriority, ChannelDef) != EKzPlayAdmission::Admitted) { return nullptr; }

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

	if (Lines.IsEmpty()) { return nullptr; }

	return StartOnPlayer(*Player, UKzLineListDialogueProvider::Create(this, Lines), ResolvedPriority, Asset->bInterruptible, bStartImmediately, AdvanceMode);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	if (Refs.IsEmpty()) { return nullptr; }

	// First-entry rule, on the first AUTHORED ref (see PlayAssetLineList).
	const UKzDialogueAsset* FirstAsset = Refs[0].Asset.LoadSynchronous();
	InChannel = ResolveChannelForEntry(InChannel, FirstAsset, Refs[0].LineId);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, FirstAsset ? FirstAsset->Priority : InheritPriority, ChannelDef);

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player) || Admit(*Player, ResolvedPriority, ChannelDef) != EKzPlayAdmission::Admitted) { return nullptr; }

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

	if (Lines.IsEmpty()) { return nullptr; }

	return StartOnPlayer(*Player, UKzLineListDialogueProvider::Create(this, Lines), ResolvedPriority, !FirstAsset || FirstAsset->bInterruptible, bStartImmediately, EKzDialogueAdvanceMode::Automatic);
}

void UKzDialogueSubsystem::StopChannel(FGameplayTag InChannel)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
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
		Player->Stop();
	}
}

void UKzDialogueSubsystem::InterruptChannel(FGameplayTag InChannel)
{
	TArray<UKzDialoguePlayer*> Matching;
	GetPlayersInScope(InChannel, Matching);
	for (UKzDialoguePlayer* Player : Matching)
	{
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
	AliasStates.Remove(AliasId);
}

void UKzDialogueSubsystem::ResetAllAliasStates()
{
	AliasStates.Reset();
}

void UKzDialogueSubsystem::RegisterTextArgumentResolver(FName Token, FKzDialogueTextArgumentResolver Resolver)
{
	if (Token.IsNone() || !Resolver.IsBound()) { return; }
	TextArgumentResolvers.Add(Token, MoveTemp(Resolver));
}

void UKzDialogueSubsystem::UnregisterTextArgumentResolver(FName Token)
{
	TextArgumentResolvers.Remove(Token);
}

const FKzDialogueTextArgumentResolver* UKzDialogueSubsystem::FindTextArgumentResolver(FName Token) const
{
	return TextArgumentResolvers.Find(Token);
}

FGuid UKzDialogueSubsystem::ResolveAliasInternal(const FKzDialogueAlias& Alias)
{
	if (Alias.LineIds.Num() == 0) { return FGuid(); }

	FAliasPlaybackState& State = AliasStates.FindOrAdd(Alias.AliasId);

	// World time, so pause and dilation stretch the cooldown too.
	if (Alias.CooldownSeconds > 0.0f && State.LastResolvedWorldTime >= 0.0)
	{
		const UWorld* World = GetWorld();
		if (World && World->TimeSince(State.LastResolvedWorldTime) < Alias.CooldownSeconds)
		{
			return FGuid();
		}
	}

	auto MarkResolved = [this, &State](const FGuid& Picked) -> FGuid
	{
		State.LastPickedLineId = Picked;
		if (const UWorld* World = GetWorld())
		{
			State.LastResolvedWorldTime = World->GetTimeSeconds();
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