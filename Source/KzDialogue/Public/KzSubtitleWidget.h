// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzSubtitleWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;
class UKzDialoguePlayer;

/** Enum to define where the affix will be placed relative to the speaker's name. */
UENUM(BlueprintType)
enum class EKzSpeakerAffixPosition : uint8
{
	Prefix,
	Suffix
};

/** Rule defining an affix to attach to the speaker's name. */
USTRUCT(BlueprintType)
struct FKzSpeakerAffixRule
{
	GENERATED_BODY()

	/** The text or symbol to add (e.g., ":", "~", "("). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles")
	FString AffixText;

	/** Whether to place it before or after the speaker's name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles")
	EKzSpeakerAffixPosition Position = EKzSpeakerAffixPosition::Suffix;

	/** Automatically inserts a space between the name and the affix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles")
	bool bAddSpace = false;
};

/**
 * A view bundle for a single dialogue channel. Holds the text widgets and animations
 * the subtitle widget uses to render lines coming from that channel.
 *
 * The subclass returns one of these per supported channel via GetViewForChannel(),
 * letting a single widget instance render N channels independently. Any member may
 * be nullptr; the base class skips the corresponding step gracefully (e.g. no
 * LineFadeIn => the player's LineEntering phase auto-completes).
 */
USTRUCT(BlueprintType)
struct FKzSubtitleChannelView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UTextBlock> SpeakerText = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UTextBlock> SubtitlesText = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UWidgetAnimation> StartFadeIn = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UWidgetAnimation> EndFadeOut = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UWidgetAnimation> LineFadeIn = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TObjectPtr<UWidgetAnimation> LineFadeOut = nullptr;

	/** True if any animation is set; controls whether the player waits for view notifications. */
	bool HasAnyAnimation() const
	{
		return StartFadeIn || EndFadeOut || LineFadeIn || LineFadeOut;
	}
};

/**
 * Multi-channel subtitle view. Binds to one or more UKzDialoguePlayer instances and
 * renders their events into per-channel UI bundles supplied by the subclass.
 *
 * The subclass is responsible for providing a FKzSubtitleChannelView for every
 * channel it wants to render via GetViewForChannel(). Channels without a view are
 * silently ignored.
 *
 * Binding:
 *  - Declarative: set ListenedChannels in the details panel; on construct the widget
 *    resolves players via the dialogue subsystem.
 *  - Imperative: call BindPlayer(P) / BindPlayers({P1, P2, ...}) at runtime.
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzSubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind a single player. No-op if already bound. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void BindPlayer(UKzDialoguePlayer* InPlayer);

	/** Bind multiple players in one call. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void BindPlayers(const TArray<UKzDialoguePlayer*>& InPlayers);

	/** Unbind a single player. No-op if not bound. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void UnbindPlayer(UKzDialoguePlayer* InPlayer);

	/** Unbind every player currently bound. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void UnbindAllPlayers();

	/** Currently bound players (weak refs, may contain stale entries; see GetBoundPlayers). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subtitles")
	void GetBoundPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const;

	/** Clears speaker and subtitle text widgets across every channel view. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	virtual void ClearTextWidgets();

protected:
	/**
	 * Channels the widget listens to. On construct, the corresponding players are
	 * resolved via the dialogue subsystem and bound automatically. Leave empty if
	 * you only want manual binding via BindPlayer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles", meta = (Categories = "Dialogue.Channel"))
	TArray<FGameplayTag> ListenedChannels;

	/** Rules applied sequentially to format the speaker's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles")
	TArray<FKzSpeakerAffixRule> SpeakerFormattingRules;

	/**
	 * Resolve the view bundle for a channel. Subclass must override this and return
	 * a populated FKzSubtitleChannelView for every supported channel.
	 *
	 * Returning an empty struct (all nullptr) is valid and means "ignore events from
	 * this channel". The base class won't apply text or play animations for it.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintPure, Category = "Dialogue|Subtitles")
	FKzSubtitleChannelView GetViewForChannel(FGameplayTag Channel) const;

	/** Optional Blueprint hooks. Override for custom per-line presentation. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Setup Line"))
	void ReceiveSetupLine(FGameplayTag Channel, const FKzDialogueLine& Line);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Show"))
	void ReceiveShow(FGameplayTag Channel);

	/** Called when the dialogue hides on a channel. May fire without a preceding EndFadeOut on hard cancellations (Abort / Interrupt). */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Hide"))
	void ReceiveHide(FGameplayTag Channel);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void OnAnimationFinished_Implementation(const UWidgetAnimation* Animation) override;

	UFUNCTION() void HandleRequestDialogueEnter(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleRequestDialogueExit(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleRequestLineEnter(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line);
	UFUNCTION() void HandleRequestLineExit(UKzDialoguePlayer* InPlayer, const FKzDialogueLine& Line);
	UFUNCTION() void HandlePaused(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleResumed(UKzDialoguePlayer* InPlayer);
	UFUNCTION() void HandleDialogueFinished(UKzDialoguePlayer* InPlayer, EKzDialogueFinishReason Reason);

private:
	/** Subscribe handlers to a player's events. */
	void BindPlayerEvents(UKzDialoguePlayer* InPlayer);

	/** Unsubscribe handlers from a player's events. */
	void UnbindPlayerEvents(UKzDialoguePlayer* InPlayer);

	/** Resolve players for ListenedChannels via the subsystem. */
	void BindFromListenedChannels();

	/** Apply line text to the given view's widgets (handling affix rules etc.). */
	void ApplyLineToView(const FKzSubtitleChannelView& View, const FKzDialogueLine& Line, FGameplayTag Channel);

	/** Start an animation and remember which player it belongs to. */
	void PlayAnimForPlayer(UWidgetAnimation* Anim, UKzDialoguePlayer* InPlayer);

	/** Look up the player whose animation just finished. */
	UKzDialoguePlayer* GetPlayerForAnim(UWidgetAnimation* Anim) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UKzDialoguePlayer>> BoundPlayers;

	/** Active animations -> originating player. Used to dispatch Notify*Finished correctly. */
	TMap<TWeakObjectPtr<UWidgetAnimation>, TWeakObjectPtr<UKzDialoguePlayer>> ActiveAnimations;
};