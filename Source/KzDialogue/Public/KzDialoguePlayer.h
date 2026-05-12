// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/TimerHandle.h"
#include "KzDialogueTypes.h"
#include "KzDialoguePlayer.generated.h"

class UAudioComponent;
class UKzDialogueProvider;

/**
 * Engine of the dialogue system. Owns the state machine, drives the provider, manages
 * audio and timing, and broadcasts events for views to react to.
 *
 * This object is intentionally NOT a widget. Multiple views (subtitles, history log,
 * 3D worldspace billboard, accessibility caption) can listen to the same player.
 *
 * Lifecycle:
 *   1. Subsystem creates one player per channel.
 *   2. Game code calls Play(Provider).
 *   3. Player walks the state machine, broadcasting events.
 *   4. Views render in response and call Notify*Finished when their animations end.
 *   5. When done, the player returns to Idle and broadcasts OnDialogueFinished.
 *
 * Views without animations: call SetUsesFadeAnimations(false) on bind.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialoguePlayer : public UObject
{
	GENERATED_BODY()

public:
	UKzDialoguePlayer();

	// -------------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------------

	/** Fallback line duration when neither the line nor its audio defines one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Player", meta = (ClampMin = 0.1))
	float DefaultDuration = 2.5f;

	/**
	 * Multiplies all line durations and audio fades. 1.0 = normal speed. Useful for
	 * console-driven debugging (Kz.Dialogue.SetSpeed) and for fast-forward features.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Player", meta = (ClampMin = 0.1, ClampMax = 10.0))
	float TimeScale = 1.0f;

	/**
	 * When true, the player waits for view-driven Notify*Finished calls to advance phases.
	 * When false, those phases auto-complete (no view is driving presentation).
	 * Set by views during binding.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Player")
	bool bWaitForViewNotifications = false;

	/**
	 * When true, audio is requested at LineEntering (overlapping the fade in).
	 * When false, audio starts only at LinePlaying.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Player")
	bool bStartAudioOnLineEnter = true;

	/** Channel this player is associated with (set by the subsystem on creation). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	FGameplayTag Channel;

	/** Priority of the currently-playing dialogue, used by the subsystem for preemption. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	int32 CurrentPriority = 0;

	// -------------------------------------------------------------------------------
	// Outgoing events
	// -------------------------------------------------------------------------------

	/** Dialogue (a whole provider session) has begun. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueStarted OnDialogueStarted;

	/** Dialogue session has ended (with reason). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueFinished OnDialogueFinished;

	/** A line has just begun (LineEntering started). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueLineEvent OnLineStarted;

	/** A line has fully ended (LineExiting completed). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueLineEvent OnLineFinished;

	/** Player has been paused. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialoguePlayerEvent OnPaused;

	/** Player has resumed from pause. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialoguePlayerEvent OnResumed;

	// Request events: views render in response and confirm with Notify*Finished.

	/** Request a global enter animation. Views must respond with NotifyEnterFinished. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialoguePlayerEvent OnRequestDialogueEnter;

	/** Request a global exit animation. Views must respond with NotifyExitFinished. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialoguePlayerEvent OnRequestDialogueExit;

	/** Request a per-line enter animation. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialogueLineEvent OnRequestLineEnter;

	/** Request a per-line exit animation. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialogueLineEvent OnRequestLineExit;

	// -------------------------------------------------------------------------------
	// Public API (commands)
	// -------------------------------------------------------------------------------

	/** Assigns a provider and immediately starts playback. Cancels current session. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Play(UKzDialogueProvider* InProvider);

	/** Assigns a provider WITHOUT starting playback. Use StartDialogue() later. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void SetProvider(UKzDialogueProvider* InProvider);

	/** Starts playback of the currently assigned provider. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void StartDialogue();

	/** Pause the player (timer, audio, view animations). No-op if Idle. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Pause();

	/** Resume from pause. No-op if not paused. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Resume();

	/** Gracefully end the current line and exit the dialogue with normal animations. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Stop();

	/** Immediately end the current dialogue without playing exit animations. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Abort();

	/** End the current dialogue marking it as Interrupted. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Interrupt();

	/** Skip the current line (cancel its timer and immediately enter LineExiting). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Skip();

	// -------------------------------------------------------------------------------
	// Specific line bindings
	// -------------------------------------------------------------------------------

	/**
	 * Bind a callback that fires only when a specific line starts playing.
	 * If LineRef points to an alias, fires whenever any of the alias's resolved lines starts.
	 *
	 * @param LineRef      The line or alias to listen for.
	 * @param Callback     Delegate invoked on match.
	 * @param bAutoUnbind  When true, the binding is removed after the first match.
	 * @return             A handle to use with UnbindSpecificLine; invalid if the bind failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|Specific Line", meta = (AutoCreateRefTerm = "Callback", AdvancedDisplay = "bAutoUnbind"))
	FGuid BindOnSpecificLineStarted(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind = true);

	/** Same as BindOnSpecificLineStarted but for OnLineFinished. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|Specific Line", meta = (AutoCreateRefTerm = "Callback", AdvancedDisplay = "bAutoUnbind"))
	FGuid BindOnSpecificLineFinished(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind = true);

	/** Cancel a binding made via BindOnSpecificLine*. Safe to call with a stale handle. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|Specific Line")
	void UnbindSpecificLine(FGuid BindingHandle);

	// -------------------------------------------------------------------------------
	// View binding helpers
	// -------------------------------------------------------------------------------

	/** Configure whether the player should wait for view animation notifications. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void SetWaitForViewNotifications(bool bInWaitForViewNotifications) { bWaitForViewNotifications = bInWaitForViewNotifications; }

	// -------------------------------------------------------------------------------
	// View notifications (called by views when their animations end)
	// -------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyEnterFinished();

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyExitFinished();

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyLineEnterFinished();

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyLineExitFinished();

	// -------------------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	EKzDialogueState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	bool IsPlaying() const { return State != EKzDialogueState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	UKzDialogueProvider* GetProvider() const { return Provider; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	FKzDialogueLine GetCurrentLine() const { return CurrentLine; }

	//~ UObject
	virtual UWorld* GetWorld() const override;
	virtual void BeginDestroy() override;

private:
	// State machine entry points. Each transitions and broadcasts as needed.
	void Enter_Entering();
	void Enter_LineEntering();
	void Enter_LinePlaying();
	void Enter_LineExiting();
	void Enter_Exiting();

	// Helpers.
	void StartLineAudio();
	void StopLineAudio(float FadeTime = 0.1f);
	float ResolveLineDuration(const FKzDialogueLine& Line) const;
	void HandleLineTimerElapsed();
	void FinishWithReason(EKzDialogueFinishReason Reason);

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueProvider> Provider = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudio = nullptr;

	UPROPERTY(Transient)
	FKzDialogueLine CurrentLine;

	UPROPERTY(Transient)
	EKzDialogueState State = EKzDialogueState::Idle;

	/** Used to restore state after Resume(). */
	EKzDialogueState StateBeforePause = EKzDialogueState::Idle;

	/** Time remaining in the line when paused, so we can rearm the timer on resume. */
	float PausedTimeRemaining = 0.0f;

	FTimerHandle LineTimerHandle;

	struct FSpecificLineBinding
	{
		FGuid Handle;
		TSet<FGuid> MatchingLineIds;
		FKzOnDialogueLineSingleEvent Callback;
		bool bAutoUnbind = true;
	};

	TArray<FSpecificLineBinding> SpecificLineStartedBindings;
	TArray<FSpecificLineBinding> SpecificLineFinishedBindings;

	/**
	 * Resolve a FKzDialogueLineRef into the set of LineIds it represents.
	 * - Line GUID: one entry (the GUID itself).
	 * - Alias GUID: every LineId the alias resolves to.
	 * Returns an empty set if the ref is invalid or the asset can't load.
	 */
	TSet<FGuid> ResolveLineRefToMatchSet(const FKzDialogueLineRef& LineRef) const;

	/** Iterate Bindings, invoke matching callbacks, prune auto-unbind ones. */
	void DispatchSpecificLineEvent(TArray<FSpecificLineBinding>& Bindings, const FKzDialogueLine& Line);
};