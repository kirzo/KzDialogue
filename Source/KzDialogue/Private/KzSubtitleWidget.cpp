// Copyright 2026 kirzo

#include "KzSubtitleWidget.h"

#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"
#include "KzDialoguePlayer.h"

void UKzSubtitleWidget::NativeConstruct()
{
	ClearTextWidgets();

	Super::NativeConstruct();

	if (IsValid(InitialPlayer))
	{
		BindPlayer(InitialPlayer);
		InitialPlayer = nullptr;
	}
}

void UKzSubtitleWidget::NativeDestruct()
{
	UnbindEvents();
	Super::NativeDestruct();
}

void UKzSubtitleWidget::BindPlayer(UKzDialoguePlayer* InPlayer)
{
	if (Player.Get() == InPlayer) { return; }

	UnbindEvents();
	Player = InPlayer;

	if (Player.IsValid())
	{
		// Tell the player it should wait for our fade animations.
		Player->SetUsesFadeAnimations(StartFadeIn != nullptr || EndFadeOut != nullptr || LineFadeIn != nullptr || LineFadeOut != nullptr);
		BindEvents();

		// Fetch the current state of the player just in case we bound late.
		EKzDialogueState CurrentState = Player->GetState();

		if (CurrentState == EKzDialogueState::Entering)
		{
			ReceiveShow();
		}
		else if (CurrentState == EKzDialogueState::LineEntering ||
			CurrentState == EKzDialogueState::LinePlaying ||
			CurrentState == EKzDialogueState::LineExiting ||
			CurrentState == EKzDialogueState::Paused) // Also handle if we bind while paused
		{
			// The player is already mid-line. Force the UI to show the current line immediately.
			ReceiveShow();
			ApplyLineToWidgets(Player->GetCurrentLine());
		}
	}
}

void UKzSubtitleWidget::ClearTextWidgets()
{
	if (SpeakerText) { SpeakerText->SetText(FText::GetEmpty()); }
	if (SubtitlesText) { SubtitlesText->SetText(FText::GetEmpty()); }
}

void UKzSubtitleWidget::BindEvents()
{
	if (!Player.IsValid()) { return; }
	Player->OnRequestDialogueEnter.AddDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueEnter);
	Player->OnRequestDialogueExit.AddDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueExit);
	Player->OnRequestLineEnter.AddDynamic(this, &UKzSubtitleWidget::HandleRequestLineEnter);
	Player->OnRequestLineExit.AddDynamic(this, &UKzSubtitleWidget::HandleRequestLineExit);
	Player->OnPaused.AddDynamic(this, &UKzSubtitleWidget::HandlePaused);
	Player->OnResumed.AddDynamic(this, &UKzSubtitleWidget::HandleResumed);
}

void UKzSubtitleWidget::UnbindEvents()
{
	if (!Player.IsValid()) { return; }
	Player->OnRequestDialogueEnter.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueEnter);
	Player->OnRequestDialogueExit.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestDialogueExit);
	Player->OnRequestLineEnter.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestLineEnter);
	Player->OnRequestLineExit.RemoveDynamic(this, &UKzSubtitleWidget::HandleRequestLineExit);
	Player->OnPaused.RemoveDynamic(this, &UKzSubtitleWidget::HandlePaused);
	Player->OnResumed.RemoveDynamic(this, &UKzSubtitleWidget::HandleResumed);
}

void UKzSubtitleWidget::HandleRequestDialogueEnter(UKzDialoguePlayer* InPlayer)
{
	ReceiveShow();
	if (StartFadeIn) { PlayAnim(StartFadeIn); }
	else { InPlayer->NotifyEnterFinished(); }
}

void UKzSubtitleWidget::HandleRequestDialogueExit(UKzDialoguePlayer* InPlayer)
{
	if (EndFadeOut) { PlayAnim(EndFadeOut); }
	else { InPlayer->NotifyExitFinished(); }
	ReceiveHide();
}

void UKzSubtitleWidget::HandleRequestLineEnter(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	ApplyLineToWidgets(Line);
	if (LineFadeIn) { PlayAnim(LineFadeIn); }
	else { InPlayer->NotifyLineEnterFinished(); }
}

void UKzSubtitleWidget::HandleRequestLineExit(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line)
{
	if (LineFadeOut) { PlayAnim(LineFadeOut); }
	else { InPlayer->NotifyLineExitFinished(); }
}

void UKzSubtitleWidget::HandlePaused(UKzDialoguePlayer* /*InPlayer*/)
{
	if (CurrentAnim) { PauseAnimation(CurrentAnim); }
}

void UKzSubtitleWidget::HandleResumed(UKzDialoguePlayer* /*InPlayer*/)
{
	if (CurrentAnim)
	{
		PlayAnimation(CurrentAnim, GetAnimationCurrentTime(CurrentAnim), 1, EUMGSequencePlayMode::Forward);
	}
}

void UKzSubtitleWidget::ApplyLineToWidgets(const FKzDialogueLine& Line)
{
	if (SpeakerText)
	{
		FString FinalSpeakerName = Line.Speaker.GetDisplayLabel().ToString();

		// Process each formatting rule in order
		for (const FKzSpeakerAffixRule& Rule : SpeakerFormattingRules)
		{
			FString SpaceStr = Rule.bAddSpace ? TEXT(" ") : TEXT("");
			if (Rule.Position == EKzSpeakerAffixPosition::Prefix)
			{
				// Example: "~" + " " + "Bob" -> "~ Bob"
				FinalSpeakerName = Rule.AffixText + SpaceStr + FinalSpeakerName;
			}
			else if (Rule.Position == EKzSpeakerAffixPosition::Suffix)
			{
				// Example: "Bob" + "" + ":" -> "Bob:"
				FinalSpeakerName = FinalSpeakerName + SpaceStr + Rule.AffixText;
			}
		}

		SpeakerText->SetText(FText::FromString(FinalSpeakerName));
	}
	if (SubtitlesText)
	{
		SubtitlesText->SetText(Line.Text);
	}
	ReceiveSetupLine(Line);
}

void UKzSubtitleWidget::PlayAnim(UWidgetAnimation* Anim)
{
	if (!Anim) { return; }
	CurrentAnim = Anim;
	PlayAnimation(Anim);
}

void UKzSubtitleWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);

	if (!Player.IsValid()) { return; }

	UKzDialoguePlayer* P = Player.Get();
	UWidgetAnimation* Finished = const_cast<UWidgetAnimation*>(Animation);

	if (Finished == StartFadeIn) { P->NotifyEnterFinished(); }
	else if (Finished == EndFadeOut) { P->NotifyExitFinished(); }
	else if (Finished == LineFadeIn) { P->NotifyLineEnterFinished(); }
	else if (Finished == LineFadeOut) { P->NotifyLineExitFinished(); }

	if (Finished == CurrentAnim) { CurrentAnim = nullptr; }
}