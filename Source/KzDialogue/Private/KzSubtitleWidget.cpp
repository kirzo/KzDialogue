// Copyright 2026 kirzo

#include "KzSubtitleWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"
#include "Internationalization/Culture.h"
#include "Misc/ConfigCacheIni.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "KzSpeakerAsset.h"
#include "Settings/KzDialogueSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** The profile lives in GameUserSettings' ini, next to the player's other settings. */
	const TCHAR* ProfileSection = TEXT("/Script/KzDialogue.KzSubtitleWidget");
	const TCHAR* ProfileKey = TEXT("SubtitleProfile");

	/** The player's profile, read from the ini the first time. */
	FKzSubtitleProfile& SubtitleProfile()
	{
		static FKzSubtitleProfile Profile = []
		{
			FKzSubtitleProfile Saved;
			FString Text;
			if (GConfig && GConfig->GetString(ProfileSection, ProfileKey, Text, GGameUserSettingsIni))
			{
				FKzSubtitleProfile::StaticStruct()->ImportText(*Text, &Saved, nullptr, PPF_None, GLog, FKzSubtitleProfile::StaticStruct()->GetName());
			}
			return Saved;
		}();
		return Profile;
	}

	FSimpleMulticastDelegate GOnSubtitleProfileChanged;

	bool IsEmptyView(const FKzSubtitleChannelView& View)
	{
		return !View.SpeakerText && !View.SubtitlesText && !View.Background && !View.HasAnyAnimation();
	}
}

// ---------------------------------------------------------------------------------------
// Row
// ---------------------------------------------------------------------------------------

FKzSubtitleChannelView UKzSubtitleRow::GetView() const
{
	FKzSubtitleChannelView View;
	View.SpeakerText = SpeakerText;
	View.SubtitlesText = SubtitlesText;
	View.Background = Background;
	View.StartFadeIn = StartFadeIn;
	View.EndFadeOut = EndFadeOut;
	View.LineFadeIn = LineFadeIn;
	View.LineFadeOut = LineFadeOut;
	return View;
}

void UKzSubtitleRow::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);

	if (UKzSubtitleWidget* Widget = SubtitleWidget.Get())
	{
		Widget->HandleAnimationFinished(this, Animation);
	}
}

// ---------------------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------------------

void UKzSubtitleWidget::SetSubtitleProfile(const FKzSubtitleProfile& Profile)
{
	SubtitleProfile() = Profile;
	if (GConfig)
	{
		FString Text;
		FKzSubtitleProfile::StaticStruct()->ExportText(Text, &Profile, nullptr, nullptr, PPF_None, nullptr);
		GConfig->SetString(ProfileSection, ProfileKey, *Text, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	GOnSubtitleProfileChanged.Broadcast();
}

FKzSubtitleProfile UKzSubtitleWidget::GetSubtitleProfile()
{
	return SubtitleProfile();
}

void UKzSubtitleWidget::NativeConstruct()
{
	ClearTextWidgets();

	Super::NativeConstruct();

	// RTL cultures (ar, he, fa, ur) mirror the whole subtitle layout automatically: alignments,
	// box flow, speaker label side. Opted in here, on the root, so subclasses cannot forget it.
	SetFlowDirectionPreference(EFlowDirectionPreference::Culture);
	FInternationalization::Get().OnCultureChanged().AddUObject(this, &UKzSubtitleWidget::HandleCultureChanged);
	GOnSubtitleProfileChanged.AddUObject(this, &UKzSubtitleWidget::HandleProfileChanged);

	BindFromListenedChannels();
}

void UKzSubtitleWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UKzDialogueSubsystem* Subsystem = World->GetSubsystem<UKzDialogueSubsystem>())
		{
			Subsystem->OnPlayerCreated.RemoveDynamic(this, &UKzSubtitleWidget::HandlePlayerCreated);
			Subsystem->OnPlayerRemoved.RemoveDynamic(this, &UKzSubtitleWidget::HandlePlayerRemoved);
		}
	}

	FInternationalization::Get().OnCultureChanged().RemoveAll(this);
	GOnSubtitleProfileChanged.RemoveAll(this);
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
	RemoveRow(InPlayer);
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

	for (const TPair<TWeakObjectPtr<UKzDialoguePlayer>, TWeakObjectPtr<UKzSubtitleRow>>& Pair : Rows)
	{
		if (UKzSubtitleRow* Row = Pair.Value.Get()) { Row->RemoveFromParent(); }
	}
	Rows.Reset();
	RowOrder.Reset();
}

void UKzSubtitleWidget::SetOwnerFilter(AActor* InOwner)
{
	OwnerFilter = InOwner;

	// The players bound so far were picked without the filter.
	UnbindAllPlayers();
	BindFromListenedChannels();
}

void UKzSubtitleWidget::ReleaseClaims(const UKzDialoguePlayer* InPlayer)
{
	// Events are already unbound, so the players moving on cannot ask this view for anything new.
	TArray<FActiveAnimation> Pending;
	for (int32 Index = ActiveAnimations.Num() - 1; Index >= 0; --Index)
	{
		const FActiveAnimation& Active = ActiveAnimations[Index];
		if (!InPlayer || Active.Player.Get() == InPlayer)
		{
			if (Active.Player.IsValid() && Active.Anim.IsValid()) { Pending.Add(Active); }
			ActiveAnimations.RemoveAt(Index);
		}
	}
	for (const FActiveAnimation& Claim : Pending)
	{
		DispatchViewAck(*Claim.Player.Get(), Claim.Anim.Get());
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
			if (AcceptsPlayer(Player))
			{
				BindPlayer(Player);
			}
		}
	}

	// ...and watch for players created later. They are broadcast before anything plays on
	// them, so the view is wired ahead of their first StartDialogue. Owned players leave with their owner.
	Subsystem->OnPlayerCreated.AddUniqueDynamic(this, &UKzSubtitleWidget::HandlePlayerCreated);
	Subsystem->OnPlayerRemoved.AddUniqueDynamic(this, &UKzSubtitleWidget::HandlePlayerRemoved);

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

bool UKzSubtitleWidget::AcceptsPlayer(const UKzDialoguePlayer* InPlayer) const
{
	if (OwnerFilter.IsValid())
	{
		return InPlayer->GetOwner() == OwnerFilter.Get();
	}

	// A per listener channel shows the owning player's conversations only.
	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(InPlayer->Channel);
	const APlayerController* OwningPlayer = GetOwningPlayer();
	if (ChannelDef && ChannelDef->Scope == EKzDialogueChannelScope::PerListener && OwningPlayer)
	{
		return InPlayer->GetOwner() == OwningPlayer;
	}
	return true;
}

void UKzSubtitleWidget::HandlePlayerCreated(FGameplayTag Channel, UKzDialoguePlayer* InPlayer)
{
	if (MatchesListenedChannels(Channel) && AcceptsPlayer(InPlayer))
	{
		BindPlayer(InPlayer);
	}
}

void UKzSubtitleWidget::HandlePlayerRemoved(FGameplayTag Channel, UKzDialoguePlayer* InPlayer)
{
	UnbindPlayer(InPlayer);
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
// Views and rows
// ---------------------------------------------------------------------------------------

FKzSubtitleChannelView UKzSubtitleWidget::FindView(UKzDialoguePlayer* InPlayer, bool bCreateRow)
{
	const FKzSubtitleChannelView Fixed = GetViewForChannel(InPlayer->Channel, InPlayer->GetOwner());
	if (!IsEmptyView(Fixed)) { return Fixed; }

	const UKzSubtitleRow* Row = FindOrAddRow(InPlayer, bCreateRow);
	return Row ? Row->GetView() : FKzSubtitleChannelView();
}

UKzSubtitleRow* UKzSubtitleWidget::FindOrAddRow(UKzDialoguePlayer* InPlayer, bool bCreate)
{
	if (const TWeakObjectPtr<UKzSubtitleRow>* Found = Rows.Find(InPlayer)) { return Found->Get(); }
	if (!bCreate || !StackPanel || !RowClass) { return nullptr; }

	// Past MaxRows the oldest row goes; its player stays rowless until its dialogue ends.
	int32 Shown = 0;
	for (const TPair<TWeakObjectPtr<UKzDialoguePlayer>, TWeakObjectPtr<UKzSubtitleRow>>& Pair : Rows)
	{
		if (Pair.Value.IsValid()) { ++Shown; }
	}
	for (int32 Index = 0; MaxRows > 0 && Shown >= MaxRows && Index < RowOrder.Num(); ++Index)
	{
		UKzDialoguePlayer* Oldest = RowOrder[Index].Get();
		TWeakObjectPtr<UKzSubtitleRow>* OldestRow = Oldest ? Rows.Find(Oldest) : nullptr;
		if (OldestRow && OldestRow->IsValid())
		{
			// Its pending claims are answered while the row still gives its view.
			ReleaseClaims(Oldest);
			OldestRow->Get()->RemoveFromParent();
			OldestRow->Reset();
			--Shown;
		}
	}

	UKzSubtitleRow* Row = CreateWidget<UKzSubtitleRow>(this, RowClass);
	if (!Row) { return nullptr; }

	Row->SubtitleWidget = this;
	StackPanel->AddChild(Row);
	Rows.Add(InPlayer, Row);
	RowOrder.Add(InPlayer);
	return Row;
}

void UKzSubtitleWidget::RemoveRow(const UKzDialoguePlayer* InPlayer)
{
	const TWeakObjectPtr<UKzDialoguePlayer> Key(const_cast<UKzDialoguePlayer*>(InPlayer));
	TWeakObjectPtr<UKzSubtitleRow> Row;
	if (Rows.RemoveAndCopyValue(Key, Row) && Row.IsValid())
	{
		Row->RemoveFromParent();
	}
	RowOrder.Remove(Key);
}

UUserWidget* UKzSubtitleWidget::GetAnimHost(const UKzDialoguePlayer* InPlayer) const
{
	const TWeakObjectPtr<UKzSubtitleRow>* Row = Rows.Find(TWeakObjectPtr<UKzDialoguePlayer>(const_cast<UKzDialoguePlayer*>(InPlayer)));
	return Row && Row->IsValid() ? static_cast<UUserWidget*>(Row->Get()) : const_cast<UKzSubtitleWidget*>(this);
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
	const FKzSubtitleChannelView View = FindView(InPlayer, true);
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

	const FKzSubtitleChannelView View = FindView(InPlayer, false);
	if (View.EndFadeOut)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.EndFadeOut, InPlayer);
	}
	ReceiveHide(InPlayer->Channel);
}

bool UKzSubtitleWidget::CanShowLine_Implementation(FGameplayTag Channel, const FKzDialogueLine& Line) const
{
	return !KzIsTextSourceEmpty(Line.Text);
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

	const FKzSubtitleChannelView View = FindView(InPlayer, true);
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
	UWidgetAnimation* FadeIn = FindView(InPlayer, false).LineFadeIn;
	UUserWidget* Host = GetAnimHost(InPlayer);
	if (FadeIn && GetPlayerForAnim(Host, FadeIn) == InPlayer)
	{
		Host->SetAnimationCurrentTime(FadeIn, FadeIn->GetEndTime());
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

	const FKzSubtitleChannelView View = FindView(InPlayer, false);
	if (View.LineFadeOut)
	{
		InPlayer->ClaimViewResponse();
		PlayAnimForPlayer(View.LineFadeOut, InPlayer);
	}
}

void UKzSubtitleWidget::HandlePaused(UKzDialoguePlayer* InPlayer)
{
	// Pause any animation that belongs to this player.
	for (const FActiveAnimation& Active : ActiveAnimations)
	{
		UUserWidget* Host = Active.Host.Get();
		UWidgetAnimation* Anim = Active.Anim.Get();
		if (Active.Player.Get() == InPlayer && Host && Anim)
		{
			Host->PauseAnimation(Anim);
		}
	}
}

void UKzSubtitleWidget::HandleResumed(UKzDialoguePlayer* InPlayer)
{
	for (const FActiveAnimation& Active : ActiveAnimations)
	{
		UUserWidget* Host = Active.Host.Get();
		UWidgetAnimation* Anim = Active.Anim.Get();
		if (Active.Player.Get() == InPlayer && Host && Anim)
		{
			Host->PlayAnimation(Anim, Host->GetAnimationCurrentTime(Anim), 1, EUMGSequencePlayMode::Forward);
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

	if (!bWasMuted)
	{
		// A normal end already played its exit, so only the texts are left, for views with no fade to hide them.
		if (Reason == EKzDialogueFinishReason::Completed || Reason == EKzDialogueFinishReason::Stopped)
		{
			const FKzSubtitleChannelView View = FindView(InPlayer, false);
			if (View.SpeakerText) { View.SpeakerText->SetText(FText::GetEmpty()); }
			if (View.SubtitlesText) { View.SubtitlesText->SetText(FText::GetEmpty()); }
		}
		else
		{
			// A hard cancel is immediate: no exit animations. ReceiveHide may arrive here without a prior EndFadeOut.
			HardResetView(InPlayer);
		}
	}

	// The row lasts as long as its player's dialogue.
	RemoveRow(InPlayer);
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
	const FKzDialogueChannelDefinition* ChannelDef = UKzDialogueSettings::Get()->FindChannel(Channel);
	const bool bClosedCaptions = ChannelDef && ChannelDef->bClosedCaptions;
	if (bClosedCaptions ? !SubtitleProfile().bShowClosedCaptions : !SubtitleProfile().bShowSubtitles)
	{
		return true;
	}

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
	// start the next phase's fade synchronously: collect first, never stop while iterating.
	TArray<TPair<UUserWidget*, UWidgetAnimation*>> ToStop;
	for (const FActiveAnimation& Active : ActiveAnimations)
	{
		UUserWidget* Host = Active.Host.Get();
		UWidgetAnimation* Anim = Active.Anim.Get();
		if (Active.Player.Get() == InPlayer && Host && Anim)
		{
			ToStop.Emplace(Host, Anim);
		}
	}
	for (const TPair<UUserWidget*, UWidgetAnimation*>& Stop : ToStop)
	{
		Stop.Key->StopAnimation(Stop.Value);
	}

	// StopAnimation queues a rewind-to-frame-0 on the shared UMG tick manager (deferred, since widget anims
	// don't use a private linker). Flush it now so that write lands BEFORE the reset below: for a fade-out
	// (frame 0 = Opacity 1) the deferred rewind would otherwise fire next tick and clobber
	// ReceiveResetChannelVisual, leaving the background visible with no text.
	GetAnimHost(InPlayer)->FlushAnimations();

	// Sweep what the stops did not untrack: stale weak anims, or animations no longer playing.
	ActiveAnimations.RemoveAll([InPlayer](const FActiveAnimation& Active)
		{
			return Active.Player.Get() == InPlayer || !Active.Player.IsValid();
		});

	const FKzSubtitleChannelView View = FindView(InPlayer, false);
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
	if (CurrentState == EKzDialogueState::Entering || CurrentState == EKzDialogueState::Waiting)
	{
		FindView(InPlayer, true);
		ReceiveShow(InPlayer->Channel);
	}
	else if (CurrentState == EKzDialogueState::LineEntering ||
		CurrentState == EKzDialogueState::LinePlaying ||
		CurrentState == EKzDialogueState::LineExiting ||
		CurrentState == EKzDialogueState::Paused)
	{
		ReceiveShow(InPlayer->Channel);
		ApplyLineToView(FindView(InPlayer, true), InPlayer->GetCurrentLine(), InPlayer->Channel);
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
		const FKzSubtitleChannelView View = FindView(Player, false);
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
		View.SubtitlesText->SetText(Line.GetDisplayText());
	}

	ApplyProfileToView(View, Line);
	ReceiveSetupLine(Channel, Line);
}

UKzSubtitleWidget::FDesignedLook UKzSubtitleWidget::FindOrCaptureDesignedLook(UWidget* Widget)
{
	if (const FDesignedLook* Found = DesignedLooks.Find(Widget)) { return *Found; }

	FDesignedLook Look;
	Look.Visibility = Widget->GetVisibility();
	if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		Look.Font = Text->GetFont();
		Look.Color = Text->GetColorAndOpacity();
	}
	else if (const UBorder* Border = Cast<UBorder>(Widget)) { Look.Color = Border->GetBrushColor(); }
	else if (const UImage* Image = Cast<UImage>(Widget)) { Look.Color = Image->GetColorAndOpacity(); }
	return DesignedLooks.Add(Widget, Look);
}

void UKzSubtitleWidget::ApplyProfileToView(const FKzSubtitleChannelView& View, const FKzDialogueLine& Line)
{
	for (UTextBlock* Text : { View.SpeakerText.Get(), View.SubtitlesText.Get() })
	{
		if (!Text) { continue; }
		FSlateFontInfo Font = FindOrCaptureDesignedLook(Text).Font;
		Font.Size *= SubtitleProfile().TextScale;
		Text->SetFont(Font);
	}

	if (UTextBlock* Speaker = View.SpeakerText)
	{
		const FDesignedLook Look = FindOrCaptureDesignedLook(Speaker);
		const UKzSpeakerAsset* Asset = Line.Speaker.Asset;
		Speaker->SetColorAndOpacity(Asset && Asset->bUseSubtitleColor ? FSlateColor(Asset->SubtitleColor) : Look.Color);
		Speaker->SetVisibility(SubtitleProfile().bShowSpeakerNames ? Look.Visibility : ESlateVisibility::Collapsed);
	}

	if (UWidget* Background = View.Background)
	{
		FLinearColor Color = FindOrCaptureDesignedLook(Background).Color.GetSpecifiedColor();
		if (SubtitleProfile().BackgroundOpacity >= 0.0f) { Color.A = SubtitleProfile().BackgroundOpacity; }
		if (UBorder* Border = Cast<UBorder>(Background)) { Border->SetBrushColor(Color); }
		else if (UImage* Image = Cast<UImage>(Background)) { Image->SetColorAndOpacity(Color); }
	}
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

void UKzSubtitleWidget::ReapplyShownLines()
{
	for (const TWeakObjectPtr<UKzDialoguePlayer>& Weak : BoundPlayers)
	{
		UKzDialoguePlayer* Player = Weak.Get();
		if (!Player || IsPlayerMuted(Player)) { continue; }

		// The states where the view shows a line, as in SyncViewToPlayer.
		const EKzDialogueState State = Player->GetState();
		if (State != EKzDialogueState::LineEntering && State != EKzDialogueState::LinePlaying && State != EKzDialogueState::LineExiting && State != EKzDialogueState::Paused) { continue; }

		ApplyLineToView(FindView(Player, false), Player->GetCurrentLine(), Player->Channel);
	}
}

void UKzSubtitleWidget::HandleProfileChanged()
{
	// Captions may have been toggled: hide or re-show those channels before restyling what is on screen.
	RefreshMuteStates();
	ReapplyShownLines();
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
	UUserWidget* Host = GetAnimHost(InPlayer);
	const int32 Existing = ActiveAnimations.IndexOfByPredicate([Host, Anim](const FActiveAnimation& Active) { return Active.Host.Get() == Host && Active.Anim.Get() == Anim; });
	if (Existing != INDEX_NONE)
	{
		UKzDialoguePlayer* Previous = ActiveAnimations[Existing].Player.Get();
		ActiveAnimations.RemoveAt(Existing);
		if (Previous && Previous != InPlayer)
		{
			DispatchViewAck(*Previous, Anim);
		}
	}

	ActiveAnimations.Add({ Host, Anim, InPlayer });
	Host->PlayAnimation(Anim);
}

UKzDialoguePlayer* UKzSubtitleWidget::GetPlayerForAnim(const UUserWidget* Host, const UWidgetAnimation* Anim) const
{
	if (!Anim) { return nullptr; }
	const FActiveAnimation* Found = ActiveAnimations.FindByPredicate([Host, Anim](const FActiveAnimation& Active) { return Active.Host.Get() == Host && Active.Anim.Get() == Anim; });
	return Found ? Found->Player.Get() : nullptr;
}

void UKzSubtitleWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);
	HandleAnimationFinished(this, Animation);
}

void UKzSubtitleWidget::HandleAnimationFinished(UUserWidget* Host, const UWidgetAnimation* Animation)
{
	UKzDialoguePlayer* Player = GetPlayerForAnim(Host, Animation);
	if (!Player) { return; }

	// Untrack BEFORE dispatching: the notify re-enters and may start the next line's fade synchronously,
	// so removing first keeps at most one tracked fade per player.
	ActiveAnimations.RemoveAll([Host, Animation](const FActiveAnimation& Active) { return Active.Host.Get() == Host && Active.Anim.Get() == Animation; });
	DispatchViewAck(*Player, Animation);
}

void UKzSubtitleWidget::DispatchViewAck(UKzDialoguePlayer& Player, const UWidgetAnimation* Anim)
{
	const FKzSubtitleChannelView View = FindView(&Player, false);
	if (Anim == View.StartFadeIn) { Player.NotifyEnterFinished(); }
	else if (Anim == View.EndFadeOut) { Player.NotifyExitFinished(); }
	else if (Anim == View.LineFadeIn) { Player.NotifyLineEnterFinished(); }
	else if (Anim == View.LineFadeOut) { Player.NotifyLineExitFinished(); }
}