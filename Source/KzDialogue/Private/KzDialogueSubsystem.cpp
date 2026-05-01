// Copyright 2026 kirzo

#include "KzDialogueSubsystem.h"

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"

namespace Kz::Tags::Dialogue
{
	UE_DEFINE_GAMEPLAY_TAG(MainChannel, "Dialogue.Channel.Main");
	UE_DEFINE_GAMEPLAY_TAG(SpeakerBase, "Dialogue.Speaker");
}

void UKzDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!DefaultChannel.IsValid())
	{
		// Hard-coded fallback.
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

UKzDialoguePlayer* UKzDialogueSubsystem::Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	if (!IsValid(Provider))
	{
		return nullptr;
	}

	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player))
	{
		return nullptr;
	}

	// If the channel is busy with a higher-priority dialogue, refuse.
	if (Player->IsPlaying() && Player->CurrentPriority > Priority)
	{
		return nullptr;
	}

	// If there's a lower-or-equal priority dialogue playing, interrupt it cleanly.
	if (Player->IsPlaying())
	{
		Player->Interrupt();
	}

	Player->CurrentPriority = Priority;
	
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

	UKzAssetDialogueProvider* Provider = UKzAssetDialogueProvider::Create(this, Asset);
	return Play(Provider, InChannel, Asset->Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueSubsystem::PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority, bool bStartImmediately)
{
	UKzDialoguePlayer* Player = GetOrCreatePlayer(InChannel);
	if (!IsValid(Player)) return nullptr;

	// Reject if the channel is busy with a higher-priority dialogue.
	if (Player->IsPlaying() && Player->CurrentPriority > Priority) return nullptr;

	if (Player->IsPlaying())
	{
		// If we are already playing a manual dialogue (like those injected by Sequencer)...
		if (UKzManualDialogueProvider* ManualProv = Cast<UKzManualDialogueProvider>(Player->GetProvider()))
		{
			if (ManualProv->Line.LineId == Line.LineId)
			{
				// Anti-spam: ignore if Sequencer triggers the exact same line twice in the same frame.
				return Player;
			}

			// Instead of interrupting the dialogue (which causes an Exiting and a new Entering state),
			// we simply inject the new line into the current Provider and force a skip.
			// This allows the widget to do a smooth LineFadeOut and LineFadeIn, without triggering StartFadeIn again.
			if (Player->GetState() != EKzDialogueState::Exiting)
			{
				Player->CurrentPriority = Priority;
				ManualProv->SetLine(Line);
				Player->Skip();
				return Player;
			}
		}
	}

	UKzManualDialogueProvider* Provider = UKzManualDialogueProvider::Create(this, Line);
	return Play(Provider, InChannel, Priority, bStartImmediately);
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