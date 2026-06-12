// Copyright 2026 kirzo

#include "KzSubtitleWidget.h"

#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"

void UKzSubtitleWidget::NativeConstruct()
{
	ClearTextWidgets();

	Super::NativeConstruct();

	BindFromListenedChannels();
}

void UKzSubtitleWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UKzDialogueSubsystem* Subsystem = World->GetSubsystem<UKzDialogueSubsystem>())
		{
			Subsystem->OnPlayerCreated.RemoveDynamic(this, &UKzSubtitleWidget::HandlePlayerCreated);
		}
	}

	UnbindAllPlayers();
	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::BindPlayer(UKzDialoguePlayer* InPlayer)
{
	if (!IsValid(InPlayer)) { return; }

	// No-op if already bound.
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Existing : BoundPlayers)
	{
		if (Existing.Get() == InPlayer) { return; }
	}

	BoundPlayers.Add(InPlayer);

	// Tell the player whether to wait for our notifications, based on the view's
	// animation setup. If the view has no animations, the player auto-completes phases.
	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	InPlayer->SetWaitForViewNotifications(View.HasAnyAnimation());

	BindPlayerEvents(InPlayer);

	// A trigger channel may already be sounding: start muted when a rule says so.
	if (IsChannelMuted(InPlayer->Channel))
	{
		MutedPlayers.Add(InPlayer);
		return;
	}

	// If we bound late and the player is mid-line, sync the view to its current state.
	SyncViewToPlayer(InPlayer);
}

void UKzSubtitleWidget::BindPlayers(const TArray<UKzDialoguePlayer*>& InPlayers)
{
	for (UKzDialoguePlayer* Player : InPlayers)
	{
		BindPlayer(Player);
	}
}

void UKzSubtitleWidget::UnbindPlayer(UKzDialoguePlayer* InPlayer)
{
	if (!InPlayer) { return; }

	UnbindPlayerEvents(InPlayer);
	MutedPlayers.Remove(InPlayer);

	BoundPlayers.RemoveAll([InPlayer](const TWeakObjectPtr<UKzDialoguePlayer>& Weak)
		{
			return Weak.Get() == InPlayer;
		});
}

void UKzSubtitleWidget::UnbindAllPlayers()
{
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		if (UKzDialoguePlayer* Player = Weak.Get())
		{
			UnbindPlayerEvents(Player);
		}
	}
	BoundPlayers.Reset();
	ActiveAnimations.Reset();
	MutedPlayers.Reset();
}

void UKzSubtitleWidget::GetBoundPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const
{
	OutPlayers.Reset();
	OutPlayers.Reserve(BoundPlayers.Num());
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		if (UKzDialoguePlayer* Player = Weak.Get())
		{
			OutPlayers.Add(Player);
		}
	}
}

void UKzSubtitleWidget::BindFromListenedChannels()
{
	if (ListenedChannels.Num() == 0) { return; }

	UWorld* World = GetWorld();
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem) { return; }

	// Bind every existing player matching a listened scope...
	for (const FGameplayTag& Scope : ListenedChannels)
	{
		TArray<UKzDialoguePlayer*> Matching;
		Subsystem->GetPlayersInScope(Scope, Matching);
		for (UKzDialoguePlayer* Player : Matching)
		{
			BindPlayer(Player);
		}
	}

	// ...and watch for players created later. They are broadcast before anything plays on
	// them, so the view is wired ahead of their first StartDialogue.
	Subsystem->OnPlayerCreated.AddDynamic(this, &UKzSubtitleWidget::HandlePlayerCreated);

	// Per-player mute evaluation during the loop above only sees the players bound so far;
	// re-evaluate now that the full set is known.
	RefreshMuteStates();
}

bool UKzSubtitleWidget::MatchesListenedChannels(FGameplayTag Channel) const
{
	for (const FGameplayTag& Scope : ListenedChannels)
	{
		if (Channel.MatchesTag(Scope)) { return true; }
	}
	return false;
}

void UKzSubtitleWidget::HandlePlayerCreated(FGameplayTag Channel, UKzDialoguePlayer* InPlayer)
{
	if (MatchesListenedChannels(Channel))
	{
		BindPlayer(InPlayer);
	}
}

void UKzSubtitleWidget::BindPlayerEvents(UKzDialoguePlayer* InPlayer)
{
	if (!InPlayer) { return; }
	InPlayer->OnRequestDialogueEnter.AddDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueEnter);
	InPlayer->OnRequestDialogueExit.AddDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueExit);
	InPlayer->OnRequestLineEnter.AddDynamic(this, &UKzSubtitleWidget::HandleRequestLineEnter);
	InPlayer->OnRequestLineExit.AddDynamic(this, &UKzSubtitleWidget::HandleRequestLineExit);
	InPlayer->OnPaused.AddDynamic(this, &UKzSubtitleWidget::HandlePaused);
	InPlayer->OnResumed.AddDynamic(this, &UKzSubtitleWidget::HandleResumed);
	InPlayer->OnDialogueFinished.AddDynamic(this, &UKzSubtitleWidget::HandleDialogueFinished);
}

void UKzSubtitleWidget::UnbindPlayerEvents(UKzDialoguePlayer* InPlayer)
{
	if (!InPlayer) { return; }
	InPlayer->OnRequestDialogueEnter.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueEnter);
	InPlayer->OnRequestDialogueExit.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueExit);
	InPlayer->OnRequestLineEnter.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestLineEnter);
	InPlayer->OnRequestLineExit.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestLineExit);
	InPlayer->OnPaused.RemoveDynamic(this, &UKzSubtitleWidget::HandlePaused);
	InPlayer->OnResumed.RemoveDynamic(this, &UKzSubtitleWidget::HandleResumed);
	InPlayer->OnDialogueFinished.RemoveDynamic(this, &UKzSubtitleWidget::HandleDialogueFinished);
}

// ---------------------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::HandleRequestDialogueEnter(UKzDialoguePlayer* InPlayer)
{
	// This player may be a mute-rule trigger: hide the views it silences before rendering.
	RefreshMuteStates();

	if (IsPlayerMuted(InPlayer))
	{
		// Render nothing, but keep the player's state machine moving (no-animation path).
		InPlayer->NotifyEnterFinished();
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	ReceiveShow(InPlayer->Channel);
	if (View.StartFadeIn) { PlayAnimForPlayer(View.StartFadeIn, InPlayer); }
	else { InPlayer->NotifyEnterFinished(); }
}

void UKzSubtitleWidget::HandleRequestDialogueExit(UKzDialoguePlayer* InPlayer)
{
	if (IsPlayerMuted(InPlayer))
	{
		// The view was already hidden when the mute kicked in.
		InPlayer->NotifyExitFinished();
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.EndFadeOut) { PlayAnimForPlayer(View.EndFadeOut, InPlayer); }
	else { InPlayer->NotifyExitFinished(); }
	ReceiveHide(InPlayer->Channel);
}

void UKzSubtitleWidget::HandleRequestLineEnter(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	if (IsPlayerMuted(InPlayer))
	{
		InPlayer->NotifyLineEnterFinished();
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	ApplyLineToView(View, Line, InPlayer->Channel);
	if (View.LineFadeIn) { PlayAnimForPlayer(View.LineFadeIn, InPlayer); }
	else { InPlayer->NotifyLineEnterFinished(); }
}

void UKzSubtitleWidget::HandleRequestLineExit(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	if (IsPlayerMuted(InPlayer))
	{
		InPlayer->NotifyLineExitFinished();
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.LineFadeOut) { PlayAnimForPlayer(View.LineFadeOut, InPlayer); }
	else { InPlayer->NotifyLineExitFinished(); }
}

void UKzSubtitleWidget::HandlePaused(UKzDialoguePlayer* InPlayer)
{
	// Pause any animation that belongs to this player.
	for (const auto& Pair : ActiveAnimations)
	{
		if (Pair.Value.Get() == InPlayer)
		{
			if (UWidgetAnimation* Anim = Pair.Key.Get())
			{
				PauseAnimation(Anim);
			}
		}
	}
}

void UKzSubtitleWidget::HandleResumed(UKzDialoguePlayer* InPlayer)
{
	for (const auto& Pair : ActiveAnimations)
	{
		if (Pair.Value.Get() == InPlayer)
		{
			if (UWidgetAnimation* Anim = Pair.Key.Get())
			{
				PlayAnimation(Anim, GetAnimationCurrentTime(Anim), 1, EUMGSequencePlayMode::Forward);
			}
		}
	}
}

void UKzSubtitleWidget::HandleDialogueFinished(UKzDialoguePlayer* InPlayer, EKzDialogueFinishReason Reason)
{
	if (!IsValid(InPlayer))
	{
		return;
	}

	const bool bWasMuted = IsPlayerMuted(InPlayer);
	MutedPlayers.Remove(InPlayer);

	// This player may have been a mute-rule trigger: re-show the views it was silencing.
	RefreshMuteStates();

	// Beyond mute bookkeeping, only react here to hard cancellations (Abort / Interrupt).
	if (Reason == EKzDialogueFinishReason::Completed || bWasMuted)
	{
		return;
	}

	// Stop any in-flight animatios.
	for (auto It = ActiveAnimations.CreateIterator(); It; ++It)
	{
		if (It->Value.Get() != InPlayer) { continue; }
		if (UWidgetAnimation* Anim = It->Key.Get())
		{
			StopAnimation(Anim);
		}
		It.RemoveCurrent();
	}

	// Snap the channel to a clean visual state without playing exit animations: a hard
	// cancel is meant to be immediate. ReceiveHide may be called here without a prior
	// EndFadeOut — subclasses that customize hide should account for that.
	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.SpeakerText)   { View.SpeakerText->SetText(FText::GetEmpty()); }
	if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }
	ReceiveHide(InPlayer->Channel);
}

// ---------------------------------------------------------------------------------------
// View-level mute
// ---------------------------------------------------------------------------------------

bool UKzSubtitleWidget::IsChannelMuted(FGameplayTag Channel) const
{
	for (const FKzSubtitleMuteRule& Rule : MuteRules)
	{
		if (!Rule.Channel.IsValid() || !Channel.MatchesAny(Rule.MutedChannels))
		{
			continue;
		}

		// The rule applies while any bound player matching its trigger is playing. Players that
		// the rule itself mutes don't count as triggers, so broad trigger scopes can't self-mute.
		for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
		{
			const UKzDialoguePlayer* Trigger = Weak.Get();
			if (Trigger && Trigger->IsPlaying() && Trigger->Channel.MatchesTag(Rule.Channel) && !Trigger->Channel.MatchesAny(Rule.MutedChannels))
			{
				return true;
			}
		}
	}
	return false;
}

bool UKzSubtitleWidget::IsPlayerMuted(const UKzDialoguePlayer* InPlayer) const
{
	return MutedPlayers.Contains(const_cast<UKzDialoguePlayer*>(InPlayer));
}

void UKzSubtitleWidget::RefreshMuteStates()
{
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		UKzDialoguePlayer* Player = Weak.Get();
		if (!Player) { continue; }

		const bool bMuted = IsChannelMuted(Player->Channel);
		const bool bWasMuted = MutedPlayers.Contains(Weak);
		if (bMuted == bWasMuted) { continue; }

		if (bMuted)
		{
			MutedPlayers.Add(Weak);
			ApplyMute(Player);
		}
		else
		{
			MutedPlayers.Remove(Weak);
			SyncViewToPlayer(Player);
		}
	}
}

void UKzSubtitleWidget::ApplyMute(UKzDialoguePlayer* InPlayer)
{
	if (!InPlayer->IsPlaying()) { return; }

	// Stop the player's in-flight animations. UMG fires OnAnimationFinished on stop, which
	// dispatches the pending Notify*Finished, so the player's state machine keeps moving.
	TArray<UWidgetAnimation*> ToStop;
	for (const auto& Pair : ActiveAnimations)
	{
		if (Pair.Value.Get() == InPlayer)
		{
			if (UWidgetAnimation* Anim = Pair.Key.Get())
			{
				ToStop.Add(Anim);
			}
		}
	}
	for (UWidgetAnimation* Anim : ToStop)
	{
		StopAnimation(Anim);
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.SpeakerText)   { View.SpeakerText->SetText(FText::GetEmpty()); }
	if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }
	ReceiveHide(InPlayer->Channel);
}

void UKzSubtitleWidget::SyncViewToPlayer(UKzDialoguePlayer* InPlayer)
{
	const EKzDialogueState CurrentState = InPlayer->GetState();
	if (CurrentState == EKzDialogueState::Entering)
	{
		ReceiveShow(InPlayer->Channel);
	}
	else if (CurrentState == EKzDialogueState::LineEntering ||
		CurrentState == EKzDialogueState::LinePlaying ||
		CurrentState == EKzDialogueState::LineExiting ||
		CurrentState == EKzDialogueState::Paused)
	{
		ReceiveShow(InPlayer->Channel);
		ApplyLineToView(GetViewForChannel(InPlayer->Channel), InPlayer->GetCurrentLine(), InPlayer->Channel);
	}
}

// ---------------------------------------------------------------------------------------
// View application
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::ClearTextWidgets()
{
	// We don't know which channels exist statically — clear whatever's bound right now.
	// Subclasses with channels that aren't bound yet should clear their own widgets if needed.
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		UKzDialoguePlayer* Player = Weak.Get();
		if (!Player) { continue; }
		const FKzSubtitleChannelView View = GetViewForChannel(Player->Channel);
		if (View.SpeakerText) { View.SpeakerText->SetText(FText::GetEmpty()); }
		if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }
	}
}

void UKzSubtitleWidget::ApplyLineToView(const FKzSubtitleChannelView& View, const FKzDialogueLine& Line, FGameplayTag Channel)
{
	if (View.SpeakerText)
	{
		FString FinalSpeakerName = Line.Speaker.GetDisplayLabel().ToString();

		for (const FKzSpeakerAffixRule& Rule : SpeakerFormattingRules)
		{
			const FString SpaceStr = Rule.bAddSpace ? TEXT(" ") : TEXT("");
			if (Rule.Position == EKzSpeakerAffixPosition::Prefix)
			{
				FinalSpeakerName = Rule.AffixText + SpaceStr + FinalSpeakerName;
			}
			else if (Rule.Position == EKzSpeakerAffixPosition::Suffix)
			{
				FinalSpeakerName = FinalSpeakerName + SpaceStr + Rule.AffixText;
			}
		}

		View.SpeakerText->SetText(FText::FromString(FinalSpeakerName));
	}

	if (View.SubtitlesText)
	{
		View.SubtitlesText->SetText(Line.Text);
	}

	ReceiveSetupLine(Channel, Line);
}

// ---------------------------------------------------------------------------------------
// Animation tracking
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::PlayAnimForPlayer(UWidgetAnimation* Anim, UKzDialoguePlayer* InPlayer)
{
	if (!Anim || !InPlayer) { return; }

	// If this animation was already in flight (e.g. hot-swap within the same channel),
	// the old entry is overwritten — UMG StopAnimation isn't strictly needed since
	// PlayAnimation restarts from the beginning, but the map must reflect the new owner.
	ActiveAnimations.Add(Anim, InPlayer);
	PlayAnimation(Anim);
}

UKzDialoguePlayer* UKzSubtitleWidget::GetPlayerForAnim(UWidgetAnimation* Anim) const
{
	if (!Anim) { return nullptr; }
	if (const TWeakObjectPtr<UKzDialoguePlayer>* Found = ActiveAnimations.Find(Anim))
	{
		return Found->Get();
	}
	return nullptr;
}

void UKzSubtitleWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);

	UWidgetAnimation* Finished = const_cast<UWidgetAnimation*>(Animation);
	UKzDialoguePlayer* Player = GetPlayerForAnim(Finished);
	if (!Player) { return; }

	// Notify the right phase based on which slot the animation occupies in the player's view.
	const FKzSubtitleChannelView View = GetViewForChannel(Player->Channel);
	if (Finished == View.StartFadeIn) { Player->NotifyEnterFinished(); }
	else if (Finished == View.EndFadeOut) { Player->NotifyExitFinished(); }
	else if (Finished == View.LineFadeIn) { Player->NotifyLineEnterFinished(); }
	else if (Finished == View.LineFadeOut) { Player->NotifyLineExitFinished(); }

	ActiveAnimations.Remove(Finished);
}