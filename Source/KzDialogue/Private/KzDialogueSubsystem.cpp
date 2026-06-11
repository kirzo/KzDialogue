// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "Settings/KzDialogueSettings.h"
#include "Algo/RandomShuffle.h"

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
	return NewPlayer;
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
		const FKzDialogueChannelDefinition* Found = Settings->FindChannel(Tag);
		if (!Found && Tag.IsValid())
		{
			UE_LOG(LogKzDialogue, Warning,
				TEXT("Playing on channel '%s' which is not defined in KzDialogueSettings. "
					"Using fallback values. Consider adding it under Project Settings -> Plugins -> KzDialogue -> Channels."),
				*Tag.ToString());
		}
		return Found;
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

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
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
	Player->SetProvider(Provider);

	if (bStartImmediately)
	{
		Player->StartDialogue();
	}

	return Player;
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, bool bStartImmediately)
{
	if (!IsValid(Asset))
	{
		return nullptr;
	}

	if (!InChannel.IsValid())
	{
		InChannel = Asset->DefaultChannel.IsValid() ? Asset->DefaultChannel : DefaultChannel;
	}

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(InheritPriority, /*AssetHintPriority=*/Asset->Priority, ChannelDef);

	UKzAssetDialogueProvider* Provider = UKzAssetDialogueProvider::Create(this, Asset);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

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

	return PlayLine(Line, InChannel, Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
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

	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/Asset->Priority, ChannelDef);

	UKzLineListDialogueProvider* Provider = UKzLineListDialogueProvider::Create(this, Lines);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately);
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

	if (!InChannel.IsValid()) { InChannel = DefaultChannel; }

	const FKzDialogueChannelDefinition* ChannelDef = FindChannelDefinition(InChannel);
	const int32 ResolvedPriority = ResolvePriority(Priority, /*AssetHintPriority=*/InheritPriority, ChannelDef);

	UKzLineListDialogueProvider* Provider = UKzLineListDialogueProvider::Create(this, Lines);
	return Play(Provider, InChannel, ResolvedPriority, bStartImmediately);
}

void UKzDialogueSubsystem::StopChannel(FGameplayTag InChannel)
{
	if (UKzDialoguePlayer* Player = FindPlayer(InChannel))
	{
		Player->Stop();
	}
}

void UKzDialogueSubsystem::StopAll()
{
	for (auto& Pair : Players)
	{
		if (IsValid(Pair.Value)) { Pair.Value->Stop(); }
	}
}

void UKzDialogueSubsystem::InterruptChannel(FGameplayTag InChannel)
{
	if (UKzDialoguePlayer* Player = FindPlayer(InChannel))
	{
		Player->Interrupt();
	}
}

void UKzDialogueSubsystem::InterruptAll()
{
	for (auto& Pair : Players)
	{
		if (IsValid(Pair.Value)) { Pair.Value->Interrupt(); }
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