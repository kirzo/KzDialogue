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

/** Set of speaker affix rules, used as a per-culture override of SpeakerFormattingRules. */
USTRUCT(BlueprintType)
struct FKzSpeakerFormattingRuleSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles")
	TArray<FKzSpeakerAffixRule> Rules;
};

/**
 * View-level mute rule: while any bound player whose channel matches Channel is playing,
 * lines from channels matching MutedChannels are not rendered by this widget. Both sides
 * match hierarchically (Dialogue.Channel.Bark covers every Bark.* channel). Purely visual:
 * the muted players keep playing (timing, audio, events) for everyone else.
 */
USTRUCT(BlueprintType)
struct FKzSubtitleMuteRule
{
	GENERATED_BODY()

	/** While a player matching this channel (or scope) is playing... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** ...channels matching these tags (or scopes) are not rendered by this widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Subtitles", meta = (Categories = "Dialogue.Channel"))
	FGameplayTagContainer MutedChannels;
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

	/**
	 * Master switch for the whole view (e.g. a "subtitles off" user setting). While disabled the
	 * widget renders nothing and stays transparent to every bound player's timing (no claims, no
	 * notifies), so dialogue audio and events keep flowing. Safe to toggle mid-dialogue: disabling
	 * stops in-flight fades (dispatching their pending notifies), re-enabling re-shows the current line.
	 * Use this instead of SetVisibility(Collapsed): a collapsed widget never ticks its animations,
	 * leaving claimed view responses unanswered and deadlocking the player before its audio starts.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subtitles")
	void SetViewEnabled(bool bEnabled);

	/** True unless the view was disabled via SetViewEnabled. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subtitles")
	bool IsViewEnabled() const { return bViewEnabled; }

	/** True when Channel is currently muted on this widget by any rule in MuteRules. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subtitles")
	bool IsChannelMuted(FGameplayTag Channel) const;

protected:
	/**
	 * Channels the widget listens to, matched hierarchically: listening to
	 * Dialogue.Channel.Bark binds every Bark.* channel, a leaf tag binds just that one.
	 * Existing players bind on construct; players created later bind automatically as
	 * they appear. Leave empty if you only want manual binding via BindPlayer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles", meta = (Categories = "Dialogue.Channel"))
	TArray<FGameplayTag> ListenedChannels;

	/**
	 * View-level mute rules, independent of ListenedChannels (order matters in neither).
	 * Example: { Channel: Dialogue.Channel.Main, Muted: [Dialogue.Channel.Bark] } hides every
	 * bark subtitle on this widget while Main is talking; barks keep sounding and re-show
	 * mid-line when Main ends.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles")
	TArray<FKzSubtitleMuteRule> MuteRules;

	/** Rules applied sequentially to format the speaker's name. Default for every culture without an override in SpeakerFormattingRulesPerCulture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles")
	TArray<FKzSpeakerAffixRule> SpeakerFormattingRules;

	/**
	 * Per-culture overrides of SpeakerFormattingRules, keyed by culture code ("ja", "es-MX").
	 * Resolution follows the culture priority chain (es-MX falls back to es before the default),
	 * so typographic conventions can change per language. An entry with an empty rule list is a
	 * valid override meaning "no affixes for this culture".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Subtitles")
	TMap<FString, FKzSpeakerFormattingRuleSet> SpeakerFormattingRulesPerCulture;

	/**
	 * Resolve the view bundle for a channel. Subclass must override this and return
	 * a populated FKzSubtitleChannelView for every supported channel.
	 *
	 * Returning an empty struct (all nullptr) is valid and means "ignore events from
	 * this channel". The base class won't apply text or play animations for it.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintPure, Category = "Dialogue|Subtitles")
	FKzSubtitleChannelView GetViewForChannel(FGameplayTag Channel) const;

	/**
	 * Gate for rendering a line on a channel. Return false to skip showing it: the dialogue keeps playing
	 * (timing, audio, events) and the view simply doesn't render that line, leaving the previous one in
	 * place. Defaults to true. Override in Blueprint to filter by the line's tags (LineHasTag), speaker, etc.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Subtitles")
	bool CanShowLine(FGameplayTag Channel, const FKzDialogueLine& Line) const;
	virtual bool CanShowLine_Implementation(FGameplayTag Channel, const FKzDialogueLine& Line) const;

	/** Optional Blueprint hooks. Override for custom per-line presentation. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Setup Line"))
	void ReceiveSetupLine(FGameplayTag Channel, const FKzDialogueLine& Line);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Show"))
	void ReceiveShow(FGameplayTag Channel);

	/** Called when the dialogue hides on a channel. May fire without a preceding EndFadeOut on hard cancellations (Abort / Interrupt). */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Hide"))
	void ReceiveHide(FGameplayTag Channel);

	/**
	 * Snap this channel's subtitle to its hidden baseline (e.g. SetRenderOpacity(0) on the subtitle root).
	 * Fired on a hard cancel (preemption / Abort / Interrupt) or mute mid-fade, before On Hide — UMG's
	 * StopAnimation only rewinds deferred (and a fade-out rewinds to Opacity 1), so zero it here directly.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Reset Channel Visual"))
	void ReceiveResetChannelVisual(FGameplayTag Channel);

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
	UFUNCTION() void HandlePlayerCreated(FGameplayTag Channel, UKzDialoguePlayer* InPlayer);

private:
	/** Subscribe handlers to a player's events. */
	void BindPlayerEvents(UKzDialoguePlayer* InPlayer);

	/** Unsubscribe handlers from a player's events. */
	void UnbindPlayerEvents(UKzDialoguePlayer* InPlayer);

	/** Bind existing players matching ListenedChannels and watch for ones created later. */
	void BindFromListenedChannels();

	/** True when Channel matches any listened scope. */
	bool MatchesListenedChannels(FGameplayTag Channel) const;

	/** True when this player's channel is in the cached muted set, or the whole view is disabled. */
	bool IsPlayerMuted(const UKzDialoguePlayer* InPlayer) const;

	/** Re-evaluates the muted set; hides views that become muted and re-shows ones that no longer are. */
	void RefreshMuteStates();

	/** Stops the player's in-flight animations (their finish notifies the player) and hides its view. */
	void ApplyMute(UKzDialoguePlayer* InPlayer);

	/** Re-shows the player's view mid-line after a mute is lifted. */
	void SyncViewToPlayer(UKzDialoguePlayer* InPlayer);

	/** Apply line text to the given view's widgets (handling affix rules etc.). */
	void ApplyLineToView(const FKzSubtitleChannelView& View, const FKzDialogueLine& Line, FGameplayTag Channel);

	/** Speaker formatting rules for the active culture: the per-culture override when one matches the culture priority chain, SpeakerFormattingRules otherwise. */
	const TArray<FKzSpeakerAffixRule>& GetActiveSpeakerFormattingRules() const;

	/** Start an animation and remember which player it belongs to. */
	void PlayAnimForPlayer(UWidgetAnimation* Anim, UKzDialoguePlayer* InPlayer);

	/** Look up the player whose animation just finished. */
	UKzDialoguePlayer* GetPlayerForAnim(UWidgetAnimation* Anim) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UKzDialoguePlayer>> BoundPlayers;

	/** Active animations -> originating player. Used to dispatch Notify*Finished correctly. */
	TMap<TWeakObjectPtr<UWidgetAnimation>, TWeakObjectPtr<UKzDialoguePlayer>> ActiveAnimations;

	/** Players whose channel is currently muted on this widget. Kept in sync by RefreshMuteStates. */
	TSet<TWeakObjectPtr<UKzDialoguePlayer>> MutedPlayers;

	/** Master switch set by SetViewEnabled. Disabled = every player behaves as muted. */
	bool bViewEnabled = true;
};