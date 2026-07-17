// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "Settings/KzDialogueSettings.h"
#include "Engine/World.h"
#include "Algo/RandomShuffle.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogue, Log, All);

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

	StopAll();
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

	if (ChannelDef)
	{
		Resolved = FMath::Clamp(Resolved, ChannelDef->MinPriority, ChannelDef->MaxPriority);
	}
	return Resolved;
}

EKzDialogueAdvanceMode UKzDialogueSubsystem::ResolveAdvanceMode(EKzDialogueAdvanceMode Override, const UKzDialogueAsset* Asset) const
{
	if (Override != EKzDialogueAdvanceMode::Inherit) { return Override; }
	if (IsValid(Asset) && Asset->AdvanceMode != EKzDialogueAdvanceMode::Inherit) { return Asset->AdvanceMode; }
	return EKzDialogueAdvanceMode::Automatic;
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
	if (!IsValid(Player))
	{
		return nullptr;
	}

	if (Player->IsPlaying())
	{
		// Higher priority always loses (refuse).
		if (Player->CurrentPriority > ResolvedPriority)
		{
			return nullptr;
		}

		// Equal-or-lower priority: both the channel and the active asset must agree to
		// interruption (AND). This keeps "non-interruptible" guarantees consistent
		// regardless of which side declared them.
		const bool bChannelAllows = !ChannelDef || ChannelDef->bAllowInterruption;
		const bool bAssetAllows = IsActiveDialogueInterruptible(Player);

		if (!bChannelAllows || !bAssetAllows)
		{
			return nullptr;
		}

		Player->Interrupt();
	}

	Player->CurrentPriority = ResolvedPriority;
	// Inherit at this level means "no asset to resolve against" (manual providers): fall to Automatic.
	Player->AdvanceMode = (AdvanceMode == EKzDialogueAdvanceMode::Inherit) ? EKzDialogueAdvanceMode::Automatic : AdvanceMode;
	Player->SetProvider(Provider);

	if (bStartImmediately)
	{
		Player->StartDialogue();
	}

	return Player;
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

void UKzDialogueSubsystem::ReleaseAssetSession(UKzDialogueAssetSession* Session)
{
	if (Session)
	{
		AssetSessions.RemoveSingle(Session);
	}
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

	// Reject if the channel is busy with a higher-priority dialogue.
	if (Player->IsPlaying() && Player->CurrentPriority > ResolvedPriority) { return nullptr; }

	if (Player->IsPlaying())
	{
		// Hot-swap inside the existing manual provider when applicable. This avoids the
		// Exiting/Entering cycle and lets the widget run a smooth LineFadeOut +
		// LineFadeIn instead of the full StartFadeIn animation. Typical use case:
		// Sequencer firing one line after another on the same track.
		if (UKzManualDialogueProvider* ManualProv = Cast<UKzManualDialogueProvider>(Player->GetProvider()))
		{
			if (ManualProv->Line.LineId == Line.LineId)
			{
				// Anti-spam: same line fired twice in the same frame.
				return Player;
			}

			if (Player->GetState() != EKzDialogueState::Exiting)
			{
				// Channel/asset interruption gating still applies, even on hot-swap.
				const bool bChannelAllows = !ChannelDef || ChannelDef->bAllowInterruption;
				const bool bAssetAllows = IsActiveDialogueInterruptible(Player);
				if (!bChannelAllows || !bAssetAllows)
				{
					return nullptr;
				}

				Player->CurrentPriority = ResolvedPriority;
				ManualProv->SetLine(Line);
				Player->Skip();
				return Player;
			}
		}
	}

	UKzManualDialogueProvider* Provider = UKzManualDialogueProvider::Create(this, Line);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately);
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
	FKzDialogueLine Line;
	if (!ResolveAssetEntry(Asset, LineId, Line))
	{
		return nullptr;
	}

	// Resolve on the authored ENTRY (not the alias-picked line) so pre-play resolution by
	// callers (async actions) always lands on the same player.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineId);

	return PlayLine(Line, InChannel, Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	if (!IsValid(Asset) || LineIds.IsEmpty()) { return nullptr; }

	// Resolve every entry up-front (aliases go through the stateful resolver, like PlayAssetLine).
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

	// First-entry rule: the session channel resolves from the first AUTHORED entry, so
	// pre-play resolution by callers stays deterministic even when entries get skipped.
	InChannel = ResolveChannelForEntry(InChannel, Asset, LineIds[0]);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/Asset->Priority, ChannelDef);

	UKzLineListDialogueProvider* Provider = UKzLineListDialogueProvider::Create(this, Lines);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately, AdvanceMode);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	if (Refs.IsEmpty()) { return nullptr; }

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

	// First-entry rule, on the first AUTHORED ref (see PlayAssetLineList).
	InChannel = ResolveChannelForEntry(InChannel, Refs[0].Asset.LoadSynchronous(), Refs[0].LineId);

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzLineListDialogueProvider* Provider = UKzLineListDialogueProvider::Create(this, Lines);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately);
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

bool UKzDialogueSubsystem::IsActiveDialogueInterruptible(const UKzDialoguePlayer* Player) const
{
	if (!IsValid(Player) || !Player->IsPlaying()) { return true; }

	// Manual providers (single ad-hoc lines) are always interruptible — they have no
	// associated asset to consult.
	const UKzDialogueProvider* Provider = Player->GetProvider();
	if (!IsValid(Provider) || Provider->IsA<UKzManualDialogueProvider>())
	{
		return true;
	}

	// Asset-backed providers consult the asset.
	if (const UKzAssetDialogueProvider* AssetProvider = Cast<UKzAssetDialogueProvider>(Provider))
	{
		return !IsValid(AssetProvider->Asset) || AssetProvider->Asset->bInterruptible;
	}

	// Unknown provider type: assume interruptible to avoid soft-locks.
	return true;
}

void UKzDialogueSubsystem::ResetAliasState(FGuid AliasId)
{
	AliasStates.Remove(AliasId);
}

void UKzDialogueSubsystem::ResetAllAliasStates()
{
	AliasStates.Reset();
}

FGuid UKzDialogueSubsystem::ResolveAliasInternal(const FKzDialogueAlias& Alias)
{
	if (Alias.LineIds.Num() == 0) { return FGuid(); }
	if (Alias.LineIds.Num() == 1) { return Alias.LineIds[0]; }

	// Lazily create the state on first resolve.
	FAliasPlaybackState& State = AliasStates.FindOrAdd(Alias.AliasId);

	// Cooldown gating. Uses world time so pause/dilation are respected naturally.
	if (Alias.CooldownSeconds > 0.0f && State.LastResolvedWorldTime >= 0.0)
	{
		const UWorld* World = GetWorld();
		if (World)
		{
			if (World->TimeSince(State.LastResolvedWorldTime) < Alias.CooldownSeconds)
			{
				// Rejected: still in cooldown window.
				return FGuid();
			}
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