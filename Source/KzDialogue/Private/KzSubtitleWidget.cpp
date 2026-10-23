// Copyright 2026 kirzo

#include "KzSubtitleWidget.h"

#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"
#include "Internationalization/Culture.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"

void UKzSubtitleWidget::NativeConstruct()
{
	ClearTextWidgets();

	Super::NativeConstruct();

	// RTL cultures (ar, he, fa, ur) mirror the whole subtitle layout automatically: alignments,
	// box flow, speaker label side. Opted in here, on the root, so subclasses cannot forget it.
	SetFlowDirectionPreference(EFlowDirectionPreference::Culture);
	FInternationalization::Get().OnCultureChanged().AddUObject(this, &UKzSubtitleWidget::HandleCultureChanged);

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

	FInternationalization::Get().OnCultureChanged().RemoveAll(this);
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

	// The player no longer needs to be told whether to wait: it discovers per phase whether any bound
	// view is presenting (via ClaimViewResponse) and advances when all claiming views finish, or when
	// none claim. Nothing to configure here.
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
	ReleaseClaims(InPlayer);
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
	MutedPlayers.Reset();
	ReleaseClaims(nullptr);
}

void UKzSubtitleWidget::ReleaseClaims(const UKzDialoguePlayer* InPlayer)
{
	// Events are already unbound, so the players moving on cannot ask this view for anything new.
	TArray<TPair<UKzDialoguePlayer*, UWidgetAnimation*>> Pending;
	for (auto It = ActiveAnimations.CreateIterator(); It; ++It)
	{
		UKzDialoguePlayer* Player = It->Value.Get();
		if (!InPlayer || Player == InPlayer)
		{
			if (Player && It->Key.IsValid()) { Pending.Emplace(Player, It->Key.Get()); }
			It.RemoveCurrent();
		}
	}
	for (const TPair<UKzDialoguePlayer*, UWidgetAnimation*>& Claim : Pending)
	{
		DispatchViewAck(*Claim.Key, Claim.Value);
	}
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
	InPlayer->OnRequestLineRevealComplete.AddDynamic(this, &UKzSubtitleWidget::HandleRequestLineRevealComplete);
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
	InPlayer->OnRequestLineRevealComplete.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestLineRevealComplete);
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

	// Muted: render nothing and stay transparent to the player's timing (don't claim, don't notify).
	if (IsPlayerMuted(InPlayer))
	{
		return;
	}

	// The first line only arrives after this fade: start blank, not on the last dialogue's texts.
	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.SpeakerText) { View.SpeakerText->SetText(FText::GetEmpty()); }
	if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }
	ReceiveShow(InPlayer->Channel);
	if (View.StartFadeIn)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.StartFadeIn, InPlayer);
	}
}

void UKzSubtitleWidget::HandleRequestDialogueExit(UKzDialoguePlayer* InPlayer)
{
	// The view was already hidden when the mute kicked in; stay transparent.
	if (IsPlayerMuted(InPlayer))
	{
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.EndFadeOut)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.EndFadeOut, InPlayer);
	}
	ReceiveHide(InPlayer->Channel);
}

bool UKzSubtitleWidget::CanShowLine_Implementation(FGameplayTag Channel, const FKzDialogueLine& Line) const
{
	return true;
}

void UKzSubtitleWidget::HandleRequestLineEnter(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	// Muted or filtered out (CanShowLine): render nothing and stay transparent: don't claim, don't notify,
	// so the view that DOES show the line drives the timing.
	if (IsPlayerMuted(InPlayer))
	{
		return;
	}
	if (!CanShowLine(InPlayer->Channel, Line))
	{
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	ApplyLineToView(View, Line, InPlayer->Channel);
	if (View.LineFadeIn)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.LineFadeIn, InPlayer);
	}
}

void UKzSubtitleWidget::HandleRequestLineRevealComplete(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	// Jump to the end, never StopAnimation: a stop rewinds to frame 0 and hides the line.
	UWidgetAnimation* FadeIn = GetViewForChannel(InPlayer->Channel).LineFadeIn;
	if (FadeIn && GetPlayerForAnim(FadeIn) == InPlayer)
	{
		SetAnimationCurrentTime(FadeIn, FadeIn->GetEndTime());
	}
}

void UKzSubtitleWidget::HandleRequestLineExit(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	// Mirror the enter gate: a line this view didn't show has nothing to fade out, stay transparent.
	if (IsPlayerMuted(InPlayer))
	{
		return;
	}
	if (!CanShowLine(InPlayer->Channel, Line))
	{
		return;
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.LineFadeOut)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.LineFadeOut, InPlayer);
	}
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

	// Beyond mute bookkeeping, only react here to hard cancellations (Abort / Interrupt): the others played their exit.
	if (Reason == EKzDialogueFinishReason::Completed || Reason == EKzDialogueFinishReason::Stopped || bWasMuted)
	{
		return;
	}

	// A hard cancel is immediate: no exit animations. ReceiveHide may arrive here without a prior EndFadeOut.
	HardResetView(InPlayer);
}

// ---------------------------------------------------------------------------------------
// View-level mute
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::SetViewEnabled(bool bEnabled)
{
	if (bViewEnabled == bEnabled) { return; }

	// Flip the flag first: the reset's StopAnimation dispatches the pending Notify, which can re-enter
	// synchronously with the player's next OnRequest*, and the handlers must already see the view disabled.
	bViewEnabled = bEnabled;

	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		UKzDialoguePlayer* Player = Weak.Get();
		if (!Player) { continue; }

		if (!bEnabled)
		{
			// Stopped fades still answer their claims, so no player is left waiting on this widget.
			if (Player->IsPlaying()) { HardResetView(Player); }
		}
		else if (!MutedPlayers.Contains(Weak))
		{
			// Re-show mid-line whatever is playing, unless a channel mute rule still hides it.
			SyncViewToPlayer(Player);
		}
	}
}

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
	// A disabled view behaves as if every player were muted: the single gate all handlers share.
	return !bViewEnabled || MutedPlayers.Contains(const_cast<UKzDialoguePlayer*>(InPlayer));
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
			if (Player->IsPlaying()) { HardResetView(Player); }
		}
		else
		{
			MutedPlayers.Remove(Weak);
			SyncViewToPlayer(Player);
		}
	}
}

void UKzSubtitleWidget::HardResetView(UKzDialoguePlayer* InPlayer)
{
	// UMG fires OnAnimationFinished on stop, which dispatches the pending Notify*Finished and may
	// start the next phase's fade synchronously: collect first, never stop while iterating the map.
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

	// StopAnimation queues a rewind-to-frame-0 on the shared UMG tick manager (deferred, since widget anims
	// don't use a private linker). Flush it now so that write lands BEFORE the reset below: for a fade-out
	// (frame 0 = Opacity 1) the deferred rewind would otherwise fire next tick and clobber
	// ReceiveResetChannelVisual, leaving the background visible with no text.
	FlushAnimations();

	// Sweep what the stops did not untrack: stale weak anims, or animations no longer playing.
	for (auto It = ActiveAnimations.CreateIterator(); It; ++It)
	{
		if (It->Value.Get() == InPlayer || !It->Value.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const FKzSubtitleChannelView View = GetViewForChannel(InPlayer->Channel);
	if (View.SpeakerText)   { View.SpeakerText->SetText(FText::GetEmpty()); }
	if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }

	// Last write wins now that the rewind has been flushed: snap the animated Opacity to its hidden baseline.
	ReceiveResetChannelVisual(InPlayer->Channel);
	ReceiveHide(InPlayer->Channel);
}

void UKzSubtitleWidget::SyncViewToPlayer(UKzDialoguePlayer* InPlayer)
{
	// Disabled view: render nothing, even when a mute lifts or a player binds mid-line.
	if (!bViewEnabled) { return; }

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
	// We don't know which channels exist statically: clear whatever's bound right now.
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
	if (View.SpeakerText && !Line.Speaker.IsValid())
	{
		View.SpeakerText->SetText(FText::GetEmpty());
	}
	else if (View.SpeakerText)
	{
		View.SpeakerText->SetText(MakeSpeakerLabel(Line));
	}

	if (View.SubtitlesText)
	{
		// Formatted at display time so a culture switch picks up the new translation.
		View.SubtitlesText->SetText(Line.GetFormattedText());
	}

	ReceiveSetupLine(Channel, Line);
}

FText UKzSubtitleWidget::MakeSpeakerLabel(const FKzDialogueLine& Line) const
{
	FString Label = Line.Speaker.GetSubtitleName(this).ToString();
	for (const FKzSpeakerAffixRule& Rule : GetActiveSpeakerFormattingRules())
	{
		const FString SpaceStr = Rule.bAddSpace ? TEXT(" ") : TEXT("");
		if (Rule.Position == EKzSpeakerAffixPosition::Prefix)
		{
			Label = Rule.AffixText + SpaceStr + Label;
		}
		else if (Rule.Position == EKzSpeakerAffixPosition::Suffix)
		{
			Label = Label + SpaceStr + Rule.AffixText;
		}
	}
	return FText::FromString(Label);
}

void UKzSubtitleWidget::HandleCultureChanged()
{
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		const UKzDialoguePlayer* Player = Weak.Get();
		if (!Player || IsPlayerMuted(Player)) { continue; }

		// The states where the view shows a line, as in SyncViewToPlayer.
		const EKzDialogueState State = Player->GetState();
		if (State != EKzDialogueState::LineEntering && State != EKzDialogueState::LinePlaying && State != EKzDialogueState::LineExiting && State != EKzDialogueState::Paused) { continue; }

		const FKzDialogueLine Line = Player->GetCurrentLine();
		const FKzSubtitleChannelView View = GetViewForChannel(Player->Channel);
		if (View.SpeakerText && Line.Speaker.IsValid())
		{
			View.SpeakerText->SetText(MakeSpeakerLabel(Line));
		}
	}
}

const TArray<FKzSpeakerAffixRule>& UKzSubtitleWidget::GetActiveSpeakerFormattingRules() const
{
	if (SpeakerFormattingRulesPerCulture.Num() > 0)
	{
		FInternationalization& I18N = FInternationalization::Get();
		const FString CurrentCulture = I18N.GetCurrentCulture()->GetName();
		for (const FString& CultureName : I18N.GetPrioritizedCultureNames(CurrentCulture))
		{
			if (const FKzSpeakerFormattingRuleSet* Override = SpeakerFormattingRulesPerCulture.Find(CultureName))
			{
				return Override->Rules;
			}
		}
	}

	return SpeakerFormattingRules;
}

// ---------------------------------------------------------------------------------------
// Animation tracking
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::PlayAnimForPlayer(UWidgetAnimation* Anim, UKzDialoguePlayer* InPlayer)
{
	if (!Anim || !InPlayer) { return; }

	// Channels sharing one view bundle: a restarted animation never reports its first run, so the
	// other player's claim is answered here before the animation changes owner.
	if (const TWeakObjectPtr<UKzDialoguePlayer>* Owner = ActiveAnimations.Find(Anim))
	{
		UKzDialoguePlayer* Previous = Owner->Get();
		ActiveAnimations.Remove(Anim);
		if (Previous && Previous != InPlayer)
		{
			DispatchViewAck(*Previous, Anim);
		}
	}

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

	// Untrack BEFORE dispatching: the notify re-enters and may start the next line's fade synchronously,
	// so removing first keeps at most one tracked fade per player.
	ActiveAnimations.Remove(Finished);
	DispatchViewAck(*Player, Finished);
}

void UKzSubtitleWidget::DispatchViewAck(UKzDialoguePlayer& Player, const UWidgetAnimation* Anim)
{
	const FKzSubtitleChannelView View = GetViewForChannel(Player.Channel);
	if (Anim == View.StartFadeIn) { Player.NotifyEnterFinished(); }
	else if (Anim == View.EndFadeOut) { Player.NotifyExitFinished(); }
	else if (Anim == View.LineFadeIn) { Player.NotifyLineEnterFinished(); }
	else if (Anim == View.LineFadeOut) { Player.NotifyLineExitFinished(); }
}