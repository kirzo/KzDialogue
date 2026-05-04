// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "Settings/KzDialogueSettings.h"

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

int32 UKzDialogueSubsystem::ResolvePriority(int32 RequestedPriority, int32 AssetHintPriority,
	const FKzDialogueChannelDefinition* ChannelDef) const
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

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel,
	int32 Priority, bool bStartImmediately)
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

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel,
	int32 Priority, bool bStartImmediately)
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