// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KzDialogueTypes.h"
#include "KzSubtitleWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;
class UKzDialoguePlayer;

/**
 * Reference subtitle view. Binds to a UKzDialoguePlayer and renders its events as
 * fade-in/out widget animations + text updates.
 *
 * This is intentionally a minimal example. Projects should subclass or replace this
 * with their own widget — the whole point of the inversion-of-control design is that
 * the player doesn't care what the view looks like.
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzSubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Bind to a player. The widget will reflect that player's events from now on.
	 * Pass nullptr to unbind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void BindPlayer(UKzDialoguePlayer* InPlayer);

	/** Currently bound player, if any. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subtitles")
	UKzDialoguePlayer* GetBoundPlayer() const { return Player.Get(); }

protected:
	/**
	 * Optional player to bind immediately upon widget creation.
	 * Exposed on spawn so you can wire it directly in the CreateWidget node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UKzDialoguePlayer> InitialPlayer = nullptr;

	/** Bind these in your derived UMG widget. Speaker is optional; only Subtitles is required. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Subtitles", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeakerText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Subtitles", meta = (BindWidget))
	TObjectPtr<UTextBlock> SubtitlesText = nullptr;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> StartFadeIn = nullptr;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> EndFadeOut = nullptr;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> LineFadeIn = nullptr;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> LineFadeOut = nullptr;

	/** Optional Blueprint hooks. Override in subclass for custom presentation. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Setup Line"))
	void ReceiveSetupLine(const FKzDialogueLine& Line);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Show"))
	void ReceiveShow();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Hide"))
	void ReceiveHide();

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void OnAnimationFinished_Implementation(const UWidgetAnimation* Animation) override;

	UFUNCTION() void HandleRequestDialogueEnter(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleRequestDialogueExit(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleRequestLineEnter(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line);
	UFUNCTION() void HandleRequestLineExit(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line);
	UFUNCTION() void HandlePaused(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleResumed(UKzDialoguePlayer* InPlayer);

	void ApplyLineToWidgets(const FKzDialogueLine& Line);
	void PlayAnim(UWidgetAnimation* Anim);

private:
	void BindEvents();
	void UnbindEvents();

	UPROPERTY(Transient)
	TWeakObjectPtr<UKzDialoguePlayer> Player;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimation> CurrentAnim = nullptr;
};