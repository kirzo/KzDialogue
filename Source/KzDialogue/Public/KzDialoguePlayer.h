// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/TimerHandle.h"
#include "Tickable.h"
#include "KzDialogueTypes.h"
#include "KzDialogueNotify.h"
#include "Net/KzDialogueNetTypes.h"
#include "KzDialoguePlayer.generated.h"

class AActor;
class UAudioComponent;
class USoundWave;
class UKzDialogueProvider;
class UKzDialogueSpeakerComponent;
struct FStreamableHandle;

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

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueSpeakerComponent> LineSpeaker = nullptr;

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

	/** The state notify's per-playback data (FKzDialogueNotifyContext::InstanceData). */
	UPROPERTY(Transient)
	FInstancedStruct InstanceData;
};

/**
 * Engine of the dialogue system: owns the state machine, drives the provider, manages audio and
 * timing, and broadcasts events for any number of views (subtitles, history, world bubbles).
 *
 * The subsystem creates one player per channel and hands it providers. On each OnRequest* event a
 * view that animates calls ClaimViewResponse and later the matching Notify*Finished; a phase nobody
 * claims advances immediately, and one whose claims never resolve advances after ViewResponseTimeout.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialoguePlayer : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

	friend class FKzDialogueDebugOverlay;

public:
	UKzDialoguePlayer();

	// -------------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------------

	/** Playback speed of line timings (durations, gaps, audio leads); audio itself plays at normal speed. Kz.Dialogue.SetSpeed drives it. */
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
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

	/** Channel this player is associated with (set by the subsystem on creation). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	FGameplayTag Channel;

	/** Whose player this is on an owned channel (set by the subsystem on creation): the speaking actor, or a local player's controller. */
	TWeakObjectPtr<AActor> Owner;

	/** The speaking actor or the local player's controller this player belongs to; none on a global channel. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	AActor* GetOwner() const { return Owner.Get(); }

	/** Priority of the currently-playing dialogue, used by the subsystem for preemption. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	int32 CurrentPriority = 0;

	/** Whether the playing dialogue lets an equal or higher priority take the channel (its asset's bInterruptible; ad-hoc lines always do). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Player")
	bool bCurrentInterruptible = true;

	// -------------------------------------------------------------------------------
	// Outgoing events
	// -------------------------------------------------------------------------------

	/** Dialogue (a whole provider session) has begun. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueStarted OnDialogueStarted;

	/** Dialogue session has ended (with reason). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueFinished OnDialogueFinished;

	/** A line has just presented: its LineStartDelay gap is over and it shows. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|Events")
	FKzOnDialogueLineEvent OnLineStarted;

	/** A line has fully ended (LineExiting completed). Not fired on Abort or Interrupt. */
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

	/** Request the line's reveal (fade in, typewriter) to jump to its end. Sent by Next in Manual mode, which moves on without waiting for the enter acks. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialogueLineEvent OnRequestLineRevealComplete;

	/** Request a per-line exit animation. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Player|View Requests")
	FKzOnDialogueLineEvent OnRequestLineExit;

	// -------------------------------------------------------------------------------
	// Public API (commands)
	// -------------------------------------------------------------------------------

	/** Assigns a provider WITHOUT starting playback, cancelling the current session; StartDialogue starts it. Blueprints play through the subsystem, which admits the request first. */
	void SetProvider(UKzDialogueProvider* InProvider);

	/** Starts playback of the currently assigned provider. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void StartDialogue();

	/** Pause the player (timers, audio, view animations). Next() is ignored while paused. No-op if Idle. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Pause();

	/** Resume from pause. No-op if not paused. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Resume();

	/** End the dialogue gracefully: the current line and the dialogue play their exit animations, no further line starts. Finishes as Stopped. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Stop();

	/** Immediately end the current dialogue without playing exit animations. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Abort();

	/** End the current dialogue marking it as Interrupted. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Interrupt();

	/** Ends a dialogue that only has its exit animations left as it would have ended (Completed, or Stopped after Stop), so taking the channel over does not report it as interrupted. No-op outside Exiting. */
	void FinishExit();

	/** Hard-stop every audio this player spawned (the current line's and released Continue-policy tails) without touching dialogue state. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void StopAllAudio(float FadeTime = 0.1f);

	/** End the showing line now and move on: drives Manual mode, cuts the line short in Automatic mode. In Manual mode a Next during the reveal completes it instead. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void Next();

	/** Lets go of a line the provider held on screen (HoldsLine), which then leaves as if its time had run out. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void ReleaseHeldLine();

	/** Goes on from Waiting, once the provider has the next line or nothing more to play. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void ResumeWaiting();

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

	/** Like BindOnSpecificLineStarted, but fires when the line starts exiting (its exit fades may still run), earlier than OnLineFinished. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|Specific Line", meta = (AutoCreateRefTerm = "Callback", AdvancedDisplay = "bAutoUnbind"))
	FGuid BindOnSpecificLineFinished(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind = true);

	/** Cancel a binding made via BindOnSpecificLine*. Safe to call with a stale handle. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|Specific Line")
	void UnbindSpecificLine(FGuid BindingHandle);

	// -------------------------------------------------------------------------------
	// View binding helpers
	// -------------------------------------------------------------------------------

	/** Call only inside an OnRequest* handler, when the view will animate that phase and then call the matching Notify*Finished. The player waits for every claim before advancing. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player")
	void ClaimViewResponse();

	// -------------------------------------------------------------------------------
	// View notifications (called by views when their animations end)
	// -------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyEnterFinished() { HandleViewAck(EKzDialogueState::Entering); }

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyExitFinished() { HandleViewAck(EKzDialogueState::Exiting); }

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyLineEnterFinished() { HandleViewAck(EKzDialogueState::LineEntering); }

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Player|View Notifications")
	void NotifyLineExitFinished() { HandleViewAck(EKzDialogueState::LineExiting); }

	// -------------------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	EKzDialogueState GetState() const { return bPaused ? EKzDialogueState::Paused : State; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	bool IsPaused() const { return bPaused; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	bool IsPlaying() const { return State != EKzDialogueState::Idle; }

	/** The line shown stays on screen because its provider holds it, such as during a choice. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	bool IsHoldingLine() const { return bHoldingLine; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	UKzDialogueProvider* GetProvider() const { return Provider; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	FKzDialogueLine GetCurrentLine() const { return CurrentLine; }

	/** The current line's id, without copying the line. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	FGuid GetCurrentLineId() const { return CurrentLine.LineId; }

	/** Current line's speaking amplitude (channel-level / whoever's talking). For per-character mouth, read the SpeakerComponent's gated GetSpeakingLevel. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	float GetSpeakingLevel() const { return SpeakingLevel; }

	/**
	 * Resolve a FKzDialogueLineRef into the set of LineIds it represents.
	 * - Line GUID: one entry (the GUID itself).
	 * - Alias GUID: every LineId the alias resolves to.
	 * Returns an empty set if the ref is invalid or the asset can't load. Public so callers that
	 * launched a ref (async actions, barks) can test whether the current line is theirs.
	 */
	TSet<FGuid> ResolveLineRefToMatchSet(const FKzDialogueLineRef& LineRef) const;

	// -------------------------------------------------------------------------------
	// Network
	// -------------------------------------------------------------------------------

	/** A copy of a server player on a client: it shows what the server's does and decides nothing. Next goes to the server; Pause, Resume, Stop, Abort and Interrupt are refused. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Player")
	bool IsProxy() const { return bProxy; }

	/** Whether this machine shows the player's lines (audio, notifies): not a dedicated server, nor a listen server for another player's own dialogue. */
	bool IsPresentedHere() const;

	/** The state its copies on the clients follow. */
	void FillNetEntry(FKzDialogueNetEntry& Entry) const;

	/** Turns a player of this client into a copy of the server's, ending what it played on its own. */
	void BecomeProxy();

	/** An idle copy plays a dialogue of this machine's own, until the server's next one turns it back. */
	void BecomeLocal() { bProxy = false; }

	/** The dialogue playing stays on this machine and never reaches the clients (Sequencer lines, per machine barks). Set by the subsystem as it starts one. */
	bool bLocalOnly = false;

	/** A copy follows its server player's new state. */
	void ApplyNetEntry(const FKzDialogueNetEntry& Entry);

	/** A copy ends its dialogue here, as its server player goes away. */
	void EndProxy();

	/** On a server, the copy of this player's own dialogue finished a view phase on the player's machine: one more ack, or an early one kept for when the phase comes. */
	void HandleRemoteViewPhaseDone(EKzDialogueState Phase, int32 InDialogueSerial, int32 InLineSerial);

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
	/** The only writer of State. Bumps StateSerial so code that just called out can tell the player moved on. */
	void SetState(EKzDialogueState NewState);

	void Enter_Entering();
	void Enter_LineEntering();
	void Enter_LinePlaying();
	void Enter_LineExiting();
	void Enter_Waiting();
	void Enter_Exiting();

	/** The line's time is up: it leaves, unless the provider holds it on screen until ReleaseHeldLine. */
	void EndLine();

	/** Ends the line shown and moves on, without the reveal step of a Manual Next. */
	void LeaveLine();

	/** On a server, sends the player's state to its copies; NewLine is the line it just took. */
	void PublishNet(const FKzDialogueLine* NewLine = nullptr);

	/** A copy leaves its line, or its waiting, for whatever its provider has now. */
	void MoveOnFromLine();

	/** A copy ends its dialogue as its server player did. */
	void EndProxyDialogue(EKzDialogueFinishReason Reason);

	/** True, with a warning, when a copy is told from outside what only its server player decides. */
	bool RefusesLocalControl(const TCHAR* Command) const;

	/** On a server, a player's own dialogue waits in each view phase for its copy on the player's machine too, as one more claim. */
	bool WaitsForRemoteView() const;
	void ClaimRemoteView();

	/** A copy of a player's own dialogue tells the server it finished a view phase. */
	void SendViewPhaseDone(EKzDialogueState Phase, int32 InDialogueSerial, int32 InLineSerial) const;

	/** The reading speed scale lines without audio are timed with: the owning player's for their own dialogue on a server, this machine's otherwise. */
	float GetReadingSpeedScale() const;

	/** A copy: how far into the line just taken it presents from, when the line reached it well under way; 0 otherwise. */
	float GetLateLineOffset() const;

	/** Presentation half of Enter_LineEntering, deferred by LineStartDelay: line-started events, audio kick-off, view enter requests. */
	void PresentLine();
	void HandleLineStartDelayElapsed();

	/** OnLineFinished, then the next line or Exiting. */
	void AdvanceAfterLineExit();
	void AdvanceToNextLine();

	/** Every claim of the current phase resolved (or none was made): move to the next phase, or on resume when paused. */
	void OnPhaseAcksDone();
	void HandleViewAck(EKzDialogueState Phase);
	void HandleViewWatchdogElapsed();

	/** Arms a timer, born paused when the player is paused. False without a world. */
	bool ArmTimer(FTimerHandle& Handle, void (UKzDialoguePlayer::*Callback)(), float Delay);
	void ClearTimer(FTimerHandle& Handle);
	void SetTimersPaused(bool bPause);

	/** Starts the line audio, honoring the line's AudioStartDelay (defers via timer when > 0). */
	void StartLineAudio();
	/** The actual audio start: create the component, bind the envelope, play. */
	void StartLineAudioNow();
	void HandleAudioDelayElapsed();
	void HandleAudioStopElapsed();
	void StopLineAudio(float FadeTime = 0.1f);

	/** Async-loads the current line's audio and the next one's, so a line start does not hitch on a synchronous load. */
	void PrefetchLineAudio();

	// Speaking level: drive a smoothed 0..1 amplitude from the current line's audio envelope.
	void BindAudioEnvelope();
	void UnbindAudioEnvelope();
	void UpdateSpeakingLevel(float DeltaTime);
	void ResolveSpeakingSpeaker();
	void ResolveLineFormatArguments();

	UFUNCTION()
	void HandleAudioEnvelope(const USoundWave* PlayingSoundWave, float EnvelopeValue);
	float ResolveLineDuration(const FKzDialogueLine& Line) const;
	EKzLineAudioInterruptionPolicy ResolveAudioPolicy(const FKzDialogueLine& Line) const;
	EKzLineAudioSpatialization ResolveAudioSpatialization(const FKzDialogueLine& Line) const;
	void ResolveOutgoingAudio(const FKzDialogueLine& OutgoingLine, const FKzDialogueLine* IncomingLine);
	void StopReleasedAudios(float FadeTime = 0.1f);
	void HandleLineTimerElapsed();
	void FinishWithReason(EKzDialogueFinishReason Reason);

	// Notify timeline, running from the line's audio start until the line exits.
	void BakeTimeline();
	bool IsTimelineRunning() const;

	/** Advances the timeline clock by DeltaTime and fires what it crossed; stops early when a notify moves the player on. */
	void EvaluateTimeline(float DeltaTime);

	void FlushTimeline();
	FKzDialogueNotifyContext BuildNotifyContext(FKzDialogueActiveNotify& Entry, float Clock, float LineDuration);

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueProvider> Provider = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudio = nullptr;

	/** Holds the prefetched audio in memory until the next line replaces it. */
	TSharedPtr<FStreamableHandle> AudioPrefetchHandle;

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

	/** Audio components from previous lines released to play past their line transition; stopped on Abort/Interrupt. */
	TArray<TWeakObjectPtr<UAudioComponent>> ReleasedAudios;

	UPROPERTY(Transient)
	FKzDialogueLine CurrentLine;

	/** Never Paused: pause is the bPaused flag on top of the phase, so phase checks keep working while paused. */
	UPROPERTY(Transient)
	EKzDialogueState State = EKzDialogueState::Idle;

	uint32 StateSerial = 0;

	/** Claims of the current phase not yet answered: reset before each OnRequest* broadcast. */
	int32 PendingViewAcks = 0;

	bool bPaused = false;

	/** The current phase's claims resolved while paused. */
	bool bAdvanceOnResume = false;

	/** Stop() was called: the current line exits and no further line starts. */
	bool bStopRequested = false;

	/** The current line got past its LineStartDelay gap (OnLineStarted fired). */
	bool bLinePresented = false;

	/** The provider holds the current line on screen until ReleaseHeldLine. */
	bool bHoldingLine = false;

	FTimerHandle LineTimerHandle;

	FTimerHandle ViewWatchdogHandle;

	/** Pending deferred audio start for the current line (AudioStartDelay > 0). */
	FTimerHandle AudioDelayTimerHandle;

	/** Pending silent pre-line gap (LineStartDelay > 0); the line presents when it elapses. */
	FTimerHandle LineStartDelayTimerHandle;

	/** Pending trim cut for the current line's audio (AudioEndTime > 0). Never armed otherwise. */
	FTimerHandle AudioStopTimerHandle;

	// Timeline runtime state, valid while IsTimelineRunning.

	UPROPERTY(Transient)
	TArray<FKzDialogueActiveNotify> ActiveNotifies;

	/** The evaluator clock: elapsed line time in seconds. */
	float TimelineClock = 0.0f;

	/** Resolved line duration in seconds, cached at bake. */
	float CachedLineDuration = 0.0f;

	/** The line timer ran out: the line reached its natural end instead of being cut. */
	bool bLineTimeElapsed = false;

	bool bProxy = false;

	/** A copy is applying its server player's state, so its own commands go through. */
	bool bApplyingNetEntry = false;

	// What the copies follow: counted here on a server, the last applied on a copy.
	int32 NetDialogueSerial = 0;
	int32 NetLineSerial = 0;
	EKzDialogueNetPhase NetPhase = EKzDialogueNetPhase::Idle;
	EKzDialogueFinishReason NetFinishReason = EKzDialogueFinishReason::Completed;
	double NetLineEnterTime = 0.0;

	/** On a server, the current phase waits for the copy on the owning player's machine. */
	bool bAwaitingRemoteView = false;

	// A copy's phase done that came before this server got to the phase; Idle when none.
	EKzDialogueState EarlyRemotePhase = EKzDialogueState::Idle;
	int32 EarlyRemoteDialogueSerial = 0;
	int32 EarlyRemoteLineSerial = 0;

	/** A copy: the server's serial of the line it shows. */
	int32 ShownNetLineSerial = 0;

	/** Seconds into the current line it presents from: a copy joining a line already under way. */
	float PresentOffset = 0.0f;

	struct FSpecificLineBinding
	{
		FGuid Handle;
		TSet<FGuid> MatchingLineIds;
		FKzOnDialogueLineSingleEvent Callback;
		bool bAutoUnbind = true;
	};

	TArray<FSpecificLineBinding> SpecificLineStartedBindings;
	TArray<FSpecificLineBinding> SpecificLineFinishedBindings;

	/** Iterate Bindings, invoke matching callbacks, prune auto-unbind ones. */
	void DispatchSpecificLineEvent(TArray<FSpecificLineBinding>& Bindings, const FKzDialogueLine& Line);
};