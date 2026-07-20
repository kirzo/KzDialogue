// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/TimerHandle.h"
#include "Tickable.h"
#include "KzDialogueTypes.h"
#include "KzDialogueNotify.h"
#include "KzDialoguePlayer.generated.h"

class UAudioComponent;
class USoundWave;
class UKzDialogueProvider;
class UKzDialogueSpeakerComponent;

/**
 * Runtime, baked-to-seconds copy of a timeline event for the currently playing line.
 * Built at LinePlaying; the flags track point firing and state activity.
 */
USTRUCT()
struct FKzDialogueActiveNotify
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueNotifyBase> Notify = nullptr;

	/** Baked window in seconds. End == Start for point notifies. */
	float Start = 0.0f;
	float End = 0.0f;

	/** Cached UKzDialogueNotifyState check. */
	bool bIsState = false;

	/** End reaches the line end, so the state ends at the flush, not by the clock. */
	bool bFlushBound = false;

	/** Mirror of the event flag: fire a skipped point at flush time. */
	bool bFireIfSkipped = false;

	/** State is currently begun. */
	bool bActive = false;

	/** Point already fired, or state already ended. */
	bool bDone = false;
};

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
class KZDIALOGUE_API UKzDialoguePlayer : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UKzDialoguePlayer();

	// -------------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------------

	/**
	 * Multiplies all line durations and audio fades. 1.0 = normal speed. Useful for
	 * console-driven debugging (Kz.Dialogue.SetSpeed) and for fast-forward features.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Player", meta = (ClampMin = 0.1, ClampMax = 10.0))
	float TimeScale = 1.0f;

	/**
	 * When true, audio is requested at LineEntering (overlapping the fade in).
	 * When false, audio starts only at LinePlaying.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Player")
	bool bStartAudioOnLineEnter = true;

	/**
	 * Automatic: each line auto-advances after its duration. Manual: each line holds until
	 * Next() is called (RPG-style). Set per session by the subsystem (asset > callsite override).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Player")
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

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

	/** Current line's speaking amplitude (channel-level, NOT gated per speaker). For per-character mouth use the SpeakerComponent. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnSpeakingLevelChanged OnSpeakingLevelChanged;

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

	/**
	 * Advance to the next line. The driver of progression in Manual advance mode; in Automatic
	 * mode it advances early, like Skip. No-op unless a line is showing. The player stays
	 * input-agnostic: callers decide when to call this.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Next();

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

	/**
	 * A view calls this while handling an OnRequest* event when it WILL present (animate) that phase and
	 * then call the matching Notify*Finished. The player waits for every claiming view before advancing
	 * (or advances immediately if none claim), so several views of different latency can share one player.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void ClaimViewResponse() { ++PendingViewAcks; }

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

	/** Current line's speaking amplitude (channel-level / whoever's talking). For per-character mouth, read the SpeakerComponent's gated GetSpeakingLevel. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	float GetSpeakingLevel() const { return SpeakingLevel; }

	//~ UObject
	virtual UWorld* GetWorld() const override;
	virtual void BeginDestroy() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual TStatId GetStatId() const override;

private:
	// State machine entry points. Each transitions and broadcasts as needed.
	void Enter_Entering();
	void Enter_LineEntering();
	void Enter_LinePlaying();
	void Enter_LineExiting();
	void Enter_Exiting();

	/** Shared by Skip() and Next(): cancel the line timer and move the showing line to LineExiting. */
	void AdvanceCurrentLine();

	/** OnLineFinished + advance to the next line or Exiting. Shared by the LineExiting auto-advance and NotifyLineExitFinished. */
	void AdvanceAfterLineExit();

	// Helpers.
	/** Starts the line audio, honoring the line's AudioStartDelay (defers via timer when > 0). */
	void StartLineAudio();
	/** The actual audio start: create the component, bind the envelope, play. */
	void StartLineAudioNow();
	void HandleAudioDelayElapsed();
	void StopLineAudio(float FadeTime = 0.1f);

	// Speaking level: drive a smoothed 0..1 amplitude from the current line's audio envelope.
	void BindAudioEnvelope();
	void UnbindAudioEnvelope();
	void UpdateSpeakingLevel(float DeltaTime);
	void ResolveSpeakingSpeaker();

	UFUNCTION()
	void HandleAudioEnvelope(const USoundWave* PlayingSoundWave, float EnvelopeValue);
	float ResolveLineDuration(const FKzDialogueLine& Line) const;
	EKzLineAudioInterruptionPolicy ResolveAudioPolicy(const FKzDialogueLine& Line) const;
	void ResolveOutgoingAudio(const FKzDialogueLine& OutgoingLine, const FKzDialogueLine* IncomingLine);
	void StopReleasedAudios(float FadeTime = 0.1f);
	void HandleLineTimerElapsed();
	void FinishWithReason(EKzDialogueFinishReason Reason);

	// Notify timeline driven during LinePlaying.
	void BakeTimeline();
	void FlushTimeline();
	FKzDialogueNotifyContext BuildNotifyContext(const FKzDialogueActiveNotify& Entry);

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueProvider> Provider = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudio = nullptr;

	/** Component whose audio envelope currently drives SpeakingLevel. */
	TWeakObjectPtr<UAudioComponent> EnvelopeBoundAudio;

	/** Gated + gained envelope target from the latest audio callback (0..1). */
	float EnvelopeTarget = 0.0f;

	/** Smoothed speaking amplitude (0..1), interpolated toward EnvelopeTarget each tick. */
	float SpeakingLevel = 0.0f;

	/** Speaker component of the current line; the player routes SpeakingLevel to it so it's gated per speaker (shared channels). */
	TWeakObjectPtr<UKzDialogueSpeakerComponent> SpeakingSpeaker;

	/** Effective speaking tuning for the current line: speaker override else project defaults. Resolved per line. */
	FKzSpeakingLevelSettings ActiveSpeakingSettings;

	/**
	 * Audio components from previous lines that were released to play past their line transition.
	 * Pruned lazily; stopped on Abort/Interrupt.
	 */
	TArray<TWeakObjectPtr<UAudioComponent>> ReleasedAudios;

	UPROPERTY(Transient)
	FKzDialogueLine CurrentLine;

	UPROPERTY(Transient)
	EKzDialogueState State = EKzDialogueState::Idle;

	/**
	 * Outstanding view presentations for the current wait-phase: reset before each OnRequest* broadcast,
	 * bumped by ClaimViewResponse(), decremented by each Notify*Finished. The phase advances when it hits 0.
	 */
	int32 PendingViewAcks = 0;

	/** Used to restore state after Resume(). */
	EKzDialogueState StateBeforePause = EKzDialogueState::Idle;

	/** Time remaining in the line when paused, so we can rearm the timer on resume. */
	float PausedTimeRemaining = 0.0f;

	FTimerHandle LineTimerHandle;

	/** Pending deferred audio start for the current line (AudioStartDelay > 0). */
	FTimerHandle AudioDelayTimerHandle;

	// Timeline runtime state (valid only while a timelined line is in LinePlaying).

	UPROPERTY(Transient)
	TArray<FKzDialogueActiveNotify> ActiveNotifies;

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueSpeakerComponent> CachedLineSpeaker = nullptr;

	/** The evaluator clock: elapsed line time in seconds. */
	float TimelineClock = 0.0f;

	/** Resolved line duration in seconds, cached at bake. */
	float CachedLineDuration = 0.0f;

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