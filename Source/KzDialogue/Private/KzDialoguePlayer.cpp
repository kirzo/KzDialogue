// Copyright 2026 kirzo

#include "KzDialoguePlayer.h"
#include "KzDialogue.h"
#include "KzDialogueProvider.h"
#include "Settings/KzDialogueSettings.h"
#include "KzDialogueAsset.h"
#include "KzDialogueFunctionLibrary.h"
#include "KzDialogueSubsystem.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueSpeakerComponent.h"

#include "AudioDevice.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	/** Sigmoid contrast around 0.5: Contrast 1 = linear, >1 pushes toward 0/1 (crisper open/close), <1 softens toward 0.5. */
	float ApplySpeakingContrast(float X, float Contrast)
	{
		if (FMath::IsNearlyEqual(Contrast, 1.0f) || X <= 0.0f || X >= 1.0f)
		{
			return X;
		}
		const float Xc = FMath::Pow(X, Contrast);
		return Xc / (Xc + FMath::Pow(1.0f - X, Contrast));
	}
}

UKzDialoguePlayer::UKzDialoguePlayer()
{
}

UWorld* UKzDialoguePlayer::GetWorld() const
{
	// Outered to the world subsystem that created it.
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

void UKzDialoguePlayer::BeginDestroy()
{
	FlushTimeline();
	StopLineAudio(0.0f);
	StopReleasedAudios(0.0f);
	// Hard teardown skips the decay ticks, so close the current speaker's mouth explicitly.
	if (UKzDialogueSpeakerComponent* Speaker = SpeakingSpeaker.Get())
	{
		Speaker->SetSpeakingLevel(0.0f);
	}
	SpeakingSpeaker = nullptr;
	ClearTimer(LineTimerHandle);
	ClearTimer(LineStartDelayTimerHandle);
	ClearTimer(ViewWatchdogHandle);
	Super::BeginDestroy();
}

// ---------------------------------------------------------------------------------------
// Public commands
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::SetProvider(UKzDialogueProvider* InProvider)
{
	if (!IsValid(InProvider)) return;

	if (IsPlaying() || IsValid(Provider))
	{
		Abort();
	}

	Provider = InProvider;
	Provider->Reset();
}

void UKzDialoguePlayer::StartDialogue()
{
	if (!IsValid(Provider) || State != EKzDialogueState::Idle)
	{
		return;
	}

	const uint32 Serial = StateSerial;
	OnDialogueStarted.Broadcast(this);
	if (Serial == StateSerial)
	{
		Enter_Entering();
	}
}

void UKzDialoguePlayer::Pause()
{
	if (State == EKzDialogueState::Idle || bPaused)
	{
		return;
	}

	bPaused = true;
	SetTimersPaused(true);
	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(true);
	}

	OnPaused.Broadcast(this);
}

void UKzDialoguePlayer::Resume()
{
	if (!bPaused)
	{
		return;
	}

	bPaused = false;
	SetTimersPaused(false);
	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(false);
	}

	const uint32 Serial = StateSerial;
	OnResumed.Broadcast(this);
	if (Serial == StateSerial && !bPaused && bAdvanceOnResume)
	{
		bAdvanceOnResume = false;
		OnPhaseAcksDone();
	}
}

void UKzDialoguePlayer::Stop()
{
	if (State == EKzDialogueState::Idle)
	{
		// A dialogue set up but never started still reports its end to whoever waits on it.
		if (IsValid(Provider)) { FinishWithReason(EKzDialogueFinishReason::Stopped); }
		return;
	}

	bStopRequested = true;
	Resume();
	if (!bStopRequested)
	{
		// The dialogue already finished while resuming.
		return;
	}

	switch (State)
	{
	case EKzDialogueState::Entering:
		Enter_Exiting();
		break;
	case EKzDialogueState::LineEntering:
	case EKzDialogueState::LinePlaying:
		ClearTimer(LineTimerHandle);
		if (bLinePresented) { Enter_LineExiting(); }
		else { Enter_Exiting(); }
		break;
	default:
		// Exiting phases: the flag ends the dialogue at the next step.
		break;
	}
}

void UKzDialoguePlayer::Abort()
{
	if (State == EKzDialogueState::Idle)
	{
		// Continue-policy tails may outlive the dialogue: a hard stop on an idle player silences the channel.
		StopReleasedAudios(0.0f);
		if (IsValid(Provider)) { FinishWithReason(EKzDialogueFinishReason::Aborted); }
		return;
	}

	StopLineAudio(0.0f);
	StopReleasedAudios(0.0f);
	FinishWithReason(EKzDialogueFinishReason::Aborted);
}

void UKzDialoguePlayer::Interrupt()
{
	if (State == EKzDialogueState::Idle)
	{
		StopReleasedAudios(0.1f);
		if (IsValid(Provider)) { FinishWithReason(EKzDialogueFinishReason::Interrupted); }
		return;
	}

	StopLineAudio(0.1f);
	StopReleasedAudios(0.1f);
	FinishWithReason(EKzDialogueFinishReason::Interrupted);
}

void UKzDialoguePlayer::StopAllAudio(float FadeTime)
{
	StopLineAudio(FadeTime);
	StopReleasedAudios(FadeTime);
}

void UKzDialoguePlayer::Next()
{
	if (bPaused)
	{
		return;
	}

	if (State == EKzDialogueState::LineEntering && !bLinePresented)
	{
		// Still in its LineStartDelay gap: the line never showed, so it leaves without exit events.
		ClearTimer(LineStartDelayTimerHandle);
		AdvanceToNextLine();
	}
	else if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		ClearTimer(LineTimerHandle);
		Enter_LineExiting();
	}
}

// ---------------------------------------------------------------------------------------
// View responses
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::ClaimViewResponse()
{
	++PendingViewAcks;

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	if (Settings && Settings->ViewResponseTimeout > 0.0f)
	{
		ArmTimer(ViewWatchdogHandle, &UKzDialoguePlayer::HandleViewWatchdogElapsed, Settings->ViewResponseTimeout);
	}
}

void UKzDialoguePlayer::HandleViewAck(EKzDialogueState Phase)
{
	// A late ack for a phase already left (stale fade) must not count against the next one.
	if (State != Phase || PendingViewAcks <= 0)
	{
		return;
	}

	if (--PendingViewAcks == 0)
	{
		ClearTimer(ViewWatchdogHandle);
		OnPhaseAcksDone();
	}
}

void UKzDialoguePlayer::HandleViewWatchdogElapsed()
{
	if (PendingViewAcks <= 0)
	{
		return;
	}

	UE_LOG(LogKzDialogue, Warning, TEXT("Channel '%s': %d view claim(s) unanswered in %s after %.1f s; advancing without them."),
		*Channel.ToString(), PendingViewAcks, *UEnum::GetValueAsString(State), UKzDialogueSettings::Get()->ViewResponseTimeout);
	PendingViewAcks = 0;
	OnPhaseAcksDone();
}

void UKzDialoguePlayer::OnPhaseAcksDone()
{
	if (bPaused)
	{
		bAdvanceOnResume = true;
		return;
	}

	switch (State)
	{
	case EKzDialogueState::Entering:
		if (bStopRequested) { Enter_Exiting(); }
		else { Enter_LineEntering(); }
		break;
	case EKzDialogueState::LineEntering:
		Enter_LinePlaying();
		break;
	case EKzDialogueState::LineExiting:
		AdvanceAfterLineExit();
		break;
	case EKzDialogueState::Exiting:
		FinishWithReason(bStopRequested ? EKzDialogueFinishReason::Stopped : EKzDialogueFinishReason::Completed);
		break;
	default:
		break;
	}
}

void UKzDialoguePlayer::AdvanceAfterLineExit()
{
	const uint32 Serial = StateSerial;
	OnLineFinished.Broadcast(this, CurrentLine);
	if (Serial == StateSerial)
	{
		AdvanceToNextLine();
	}
}

void UKzDialoguePlayer::AdvanceToNextLine()
{
	if (!bStopRequested && IsValid(Provider) && Provider->HasNext())
	{
		Enter_LineEntering();
	}
	else
	{
		Enter_Exiting();
	}
}

// ---------------------------------------------------------------------------------------
// State entries
// ---------------------------------------------------------------------------------------

// Every broadcast may move the player on (a listener skipping, stopping or replacing the dialogue):
// each entry captures the serial after its transition and stops as soon as it changed.

void UKzDialoguePlayer::SetState(EKzDialogueState NewState)
{
	State = NewState;
	++StateSerial;
	bAdvanceOnResume = false;
	ClearTimer(ViewWatchdogHandle);
}

void UKzDialoguePlayer::Enter_Entering()
{
	SetState(EKzDialogueState::Entering);
	const uint32 Serial = StateSerial;

	PendingViewAcks = 0;
	OnRequestDialogueEnter.Broadcast(this);
	if (Serial == StateSerial && PendingViewAcks == 0)
	{
		OnPhaseAcksDone();
	}
}

void UKzDialoguePlayer::Enter_LineEntering()
{
	if (!IsValid(Provider) || !Provider->HasNext())
	{
		Enter_Exiting();
		return;
	}

	SetState(EKzDialogueState::LineEntering);
	bLinePresented = false;

	const FKzDialogueLine PreviousLine = MoveTemp(CurrentLine);
	CurrentLine = Provider->Advance();

	// Decide what to do with the previous line's audio now that we know the incoming one.
	ResolveOutgoingAudio(PreviousLine, &CurrentLine);

	// Ambient template arguments go in before anyone sees the line, so every event and view gets a copy that formats.
	ResolveLineFormatArguments();

	// Silent pre-line gap: nothing presents (no subtitle, audio or view enter) until it elapses.
	const float Gap = FMath::Max(0.0f, CurrentLine.LineStartDelay) / FMath::Max(0.1f, TimeScale);
	if (Gap > UE_KINDA_SMALL_NUMBER && ArmTimer(LineStartDelayTimerHandle, &UKzDialoguePlayer::HandleLineStartDelayElapsed, Gap))
	{
		return;
	}

	PresentLine();
}

void UKzDialoguePlayer::HandleLineStartDelayElapsed()
{
	if (State == EKzDialogueState::LineEntering && !bLinePresented)
	{
		PresentLine();
	}
}

void UKzDialoguePlayer::PresentLine()
{
	const uint32 Serial = StateSerial;
	bLinePresented = true;

	OnLineStarted.Broadcast(this, CurrentLine);
	if (Serial != StateSerial) { return; }

	DispatchSpecificLineEvent(SpecificLineStartedBindings, CurrentLine);
	if (Serial != StateSerial) { return; }

	if (bStartAudioOnLineEnter)
	{
		StartLineAudio();
	}

	PendingViewAcks = 0;
	OnRequestLineEnter.Broadcast(this, CurrentLine);
	if (Serial == StateSerial && PendingViewAcks == 0)
	{
		OnPhaseAcksDone();
	}
}

void UKzDialoguePlayer::Enter_LinePlaying()
{
	SetState(EKzDialogueState::LinePlaying);

	if (!bStartAudioOnLineEnter)
	{
		StartLineAudio();
	}

	BakeTimeline();

	// Manual mode holds the line on screen until Next(); no auto-advance timer.
	if (AdvanceMode == EKzDialogueAdvanceMode::Manual)
	{
		return;
	}

	if (!ArmTimer(LineTimerHandle, &UKzDialoguePlayer::HandleLineTimerElapsed, ResolveLineDuration(CurrentLine)))
	{
		// No world (unlikely in normal use): advance immediately.
		HandleLineTimerElapsed();
	}
}

void UKzDialoguePlayer::Enter_LineExiting()
{
	SetState(EKzDialogueState::LineExiting);
	const uint32 Serial = StateSerial;

	// Audio still waiting on its AudioStartDelay must never start now.
	ClearTimer(AudioDelayTimerHandle);
	ClearTimer(LineStartDelayTimerHandle);

	FlushTimeline();
	if (Serial != StateSerial) { return; }

	PendingViewAcks = 0;
	OnRequestLineExit.Broadcast(this, CurrentLine);
	if (Serial != StateSerial) { return; }

	DispatchSpecificLineEvent(SpecificLineFinishedBindings, CurrentLine);
	if (Serial == StateSerial && PendingViewAcks == 0)
	{
		OnPhaseAcksDone();
	}
}

void UKzDialoguePlayer::Enter_Exiting()
{
	SetState(EKzDialogueState::Exiting);
	const uint32 Serial = StateSerial;

	ResolveOutgoingAudio(CurrentLine, nullptr);
	PendingViewAcks = 0;
	OnRequestDialogueExit.Broadcast(this);
	if (Serial == StateSerial && PendingViewAcks == 0)
	{
		OnPhaseAcksDone();
	}
}

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

bool UKzDialoguePlayer::ArmTimer(FTimerHandle& Handle, void (UKzDialoguePlayer::*Callback)(), float Delay)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	TimerManager.SetTimer(Handle, this, Callback, Delay, /*loop=*/false);
	if (bPaused)
	{
		TimerManager.PauseTimer(Handle);
	}
	return true;
}

void UKzDialoguePlayer::ClearTimer(FTimerHandle& Handle)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Handle);
	}
}

void UKzDialoguePlayer::SetTimersPaused(bool bPause)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	FTimerHandle* Handles[] = { &LineTimerHandle, &LineStartDelayTimerHandle, &AudioDelayTimerHandle, &AudioStopTimerHandle, &ViewWatchdogHandle };
	for (FTimerHandle* Handle : Handles)
	{
		if (bPause) { TimerManager.PauseTimer(*Handle); }
		else { TimerManager.UnPauseTimer(*Handle); }
	}
}

void UKzDialoguePlayer::HandleLineTimerElapsed()
{
	if (State == EKzDialogueState::LinePlaying)
	{
		Enter_LineExiting();
	}
}

void UKzDialoguePlayer::FinishWithReason(EKzDialogueFinishReason Reason)
{
	// A state ending in the flush may itself have finished or replaced the dialogue.
	const uint32 Serial = StateSerial;
	FlushTimeline();
	if (Serial != StateSerial)
	{
		return;
	}

	ClearTimer(LineTimerHandle);
	ClearTimer(LineStartDelayTimerHandle);
	SetState(EKzDialogueState::Idle);
	bPaused = false;
	bStopRequested = false;
	bLinePresented = false;
	PendingViewAcks = 0;
	Provider = nullptr;
	CurrentLine = FKzDialogueLine{};

	OnDialogueFinished.Broadcast(this, Reason);
}

float UKzDialoguePlayer::ResolveLineDuration(const FKzDialogueLine& Line) const
{
	const USoundBase* Sound = Line.Audio.Get();
	const float Duration = Line.ResolveTotalDuration(Sound ? Sound->GetDuration() : 0.0f);

	// Apply TimeScale, but clamp to a sane minimum so we never set a 0-duration timer.
	return FMath::Max(0.1f, Duration / FMath::Max(0.1f, TimeScale));
}

void UKzDialoguePlayer::StartLineAudio()
{
	// Subtitles lead the voice: the whole audio start waits AudioStartDelay, compressed by TimeScale like every line timing.
	const float Delay = FMath::Max(0.0f, CurrentLine.AudioStartDelay) / FMath::Max(0.1f, TimeScale);
	if (Delay > UE_KINDA_SMALL_NUMBER && !CurrentLine.Audio.IsNull() && ArmTimer(AudioDelayTimerHandle, &UKzDialoguePlayer::HandleAudioDelayElapsed, Delay))
	{
		return;
	}

	StartLineAudioNow();
}

void UKzDialoguePlayer::HandleAudioDelayElapsed()
{
	// Every path that ends the line clears the timer, but stay defensive: a late fire must not
	// start audio for a line that is no longer presenting.
	if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		StartLineAudioNow();
	}
}

void UKzDialoguePlayer::StartLineAudioNow()
{
	USoundBase* Sound = CurrentLine.Audio.IsNull() ? nullptr : CurrentLine.Audio.LoadSynchronous();
	if (!Sound)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Attached mode needs a live speaker component; without one (narration line, actor absent,
	// Sequencer preview world) the line falls back to 2D.
	USceneComponent* AttachTarget = nullptr;
	FName AttachSocket = NAME_None;
	if (ResolveAudioSpatialization(CurrentLine) == EKzLineAudioSpatialization::AttachedToSpeaker && CurrentLine.Speaker.IsValid())
	{
		if (UKzDialogueSpeakerComponent* SpeakerComponent = UKzDialogueSpeakerComponent::FindSpeaker(this, CurrentLine.Speaker.Asset))
		{
			AttachTarget = SpeakerComponent->GetAudioAttachComponent(AttachSocket);
		}
	}

	// Neither path may go through the SpawnSound* helpers: those auto-play, and the envelope
	// follower must be bound BEFORE Play (see BindAudioEnvelope below). Create silent, Play at the end.
	if (AttachTarget)
	{
		// SpawnSoundAttached's creation, minus its final Play.
		FAudioDevice::FCreateComponentParams Params(World, AttachTarget->GetOwner());
		Params.SetLocation(AttachTarget->GetSocketLocation(AttachSocket));
		Params.bStopWhenOwnerDestroyed = true;
		ActiveAudio = FAudioDevice::CreateComponent(Sound, Params);
		if (!IsValid(ActiveAudio))
		{
			return;
		}
		ActiveAudio->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachSocket);
		ActiveAudio->bAllowSpatialization = World->IsGameWorld();
		ActiveAudio->bIsUISound = !World->IsGameWorld();
	}
	else
	{
		// CreateSound2D (not SpawnSound2D) for the same bind-before-play reason.
		ActiveAudio = UGameplayStatics::CreateSound2D(World, Sound);
		if (!IsValid(ActiveAudio))
		{
			return;
		}

		// The UI-sound flag doubles as "keep playing while the game isn't ticking". In game worlds we
		// clear it so dialogue pauses with SetGamePaused; in the editor world (Sequencer preview) the
		// game never ticks, so clearing it would mute preview playback entirely.
		ActiveAudio->bIsUISound = !World->IsGameWorld();
	}
	ActiveAudio->bAutoDestroy = true;

	// Bind BEFORE Play: the engine captures whether to run the envelope follower (bUpdateSingleEnvelopeValue)
	// from OnAudioSingleEnvelopeValue.IsBound() at sound-start time, so binding after Play never fires it.
	BindAudioEnvelope();
	ResolveSpeakingSpeaker(); // resolve speaker + tuning before the first envelope callback can land
	ActiveAudio->Play(FMath::Max(0.0f, CurrentLine.AudioStartTime));
	if (bPaused)
	{
		// A listener paused while the line was presenting.
		ActiveAudio->SetPaused(true);
	}

	// Cut at AudioEndTime (0 = natural end, nothing armed). Audio plays in real time, so the cut is not scaled by TimeScale.
	if (CurrentLine.AudioEndTime > 0.0f)
	{
		const float StopIn = FMath::Max(0.05f, CurrentLine.AudioEndTime - FMath::Max(0.0f, CurrentLine.AudioStartTime));
		ArmTimer(AudioStopTimerHandle, &UKzDialoguePlayer::HandleAudioStopElapsed, StopIn);
	}
}

void UKzDialoguePlayer::HandleAudioStopElapsed()
{
	// Same teardown as a policy stop, with a short fade so the cut does not click.
	if (IsValid(ActiveAudio))
	{
		StopLineAudio(0.1f);
	}
}

void UKzDialoguePlayer::StopLineAudio(float FadeTime)
{
	// Hard teardowns also cancel a pending deferred start and trim cut.
	ClearTimer(AudioDelayTimerHandle);
	ClearTimer(AudioStopTimerHandle);

	UnbindAudioEnvelope();
	if (IsValid(ActiveAudio))
	{
		if (FadeTime > 0.0f) { ActiveAudio->FadeOut(FadeTime, 0.0f); }
		else { ActiveAudio->Stop(); }
	}
	ActiveAudio = nullptr;
}

void UKzDialoguePlayer::BindAudioEnvelope()
{
	if (EnvelopeBoundAudio.Get() == ActiveAudio)
	{
		return;
	}
	UnbindAudioEnvelope();
	if (IsValid(ActiveAudio))
	{
		// Note: OnAudioSingleEnvelopeValue only fires if the wave has amplitude envelope analysis enabled.
		ActiveAudio->OnAudioSingleEnvelopeValue.AddDynamic(this, &UKzDialoguePlayer::HandleAudioEnvelope);
		EnvelopeBoundAudio = ActiveAudio;
	}
}

void UKzDialoguePlayer::UnbindAudioEnvelope()
{
	if (UAudioComponent* Audio = EnvelopeBoundAudio.Get())
	{
		Audio->OnAudioSingleEnvelopeValue.RemoveDynamic(this, &UKzDialoguePlayer::HandleAudioEnvelope);
	}
	EnvelopeBoundAudio = nullptr;
	EnvelopeTarget = 0.0f;
}

void UKzDialoguePlayer::HandleAudioEnvelope(const USoundWave* PlayingSoundWave, float EnvelopeValue)
{
	// Gate the silence floor and gain into 0..1, then push toward 0/1 by Contrast so the mouth opens/closes
	// crisply instead of hovering mid-open on long phrases. Smoothing happens in UpdateSpeakingLevel.
	const float Gained = (EnvelopeValue <= ActiveSpeakingSettings.Threshold) ? 0.0f : FMath::Min(EnvelopeValue * ActiveSpeakingSettings.Gain, 1.0f);
	EnvelopeTarget = ApplySpeakingContrast(Gained, ActiveSpeakingSettings.Contrast);
}

void UKzDialoguePlayer::UpdateSpeakingLevel(float DeltaTime)
{
	// No live audio -> let the jaw fall shut.
	if (!EnvelopeBoundAudio.IsValid() || !EnvelopeBoundAudio->IsPlaying())
	{
		EnvelopeTarget = 0.0f;
	}

	// Rising uses a CONSTANT rate so the first open from 0 ramps instead of snapping: FInterpTo is exponential
	// and takes a huge proportional bite when the gap is big (0 -> high). Falling keeps the eased close.
	const float NewLevel = (EnvelopeTarget > SpeakingLevel)
		? FMath::FInterpConstantTo(SpeakingLevel, EnvelopeTarget, DeltaTime, ActiveSpeakingSettings.AttackSpeed)
		: FMath::FInterpTo(SpeakingLevel, EnvelopeTarget, DeltaTime, ActiveSpeakingSettings.ReleaseSpeed);
	if (!FMath::IsNearlyEqual(NewLevel, SpeakingLevel))
	{
		SpeakingLevel = NewLevel;
		OnSpeakingLevelChanged.Broadcast(SpeakingLevel);

		// Route to the current speaker so it's gated per speaker (several speakers can share this channel/player).
		if (UKzDialogueSpeakerComponent* Speaker = SpeakingSpeaker.Get())
		{
			Speaker->SetSpeakingLevel(SpeakingLevel);
		}
	}
}

void UKzDialoguePlayer::ResolveLineFormatArguments()
{
	// Ambient token resolvers only run for lines whose pattern actually uses arguments.
	TArray<FString> ArgumentNames;
	FTextFormat(CurrentLine.Text).GetFormatArgumentNames(ArgumentNames);
	if (ArgumentNames.Num() == 0) { return; }

	UWorld* World = GetWorld();
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem) { return; }

	for (const FString& ArgumentName : ArgumentNames)
	{
		// Explicit per-play arguments (set by the call site on its line copy) win over ambient resolvers.
		if (CurrentLine.FormatArguments.Contains(ArgumentName)) { continue; }

		const FKzDialogueTextArgumentResolver* Resolver = Subsystem->FindTextArgumentResolver(FName(*ArgumentName));
		if (Resolver && Resolver->IsBound())
		{
			CurrentLine.FormatArguments.Add(ArgumentName, FFormatArgumentValue(Resolver->Execute(CurrentLine)));
			continue;
		}

		// Data tier: named assets addressed by token ("{Kirzo}", "{Kirzo:given}"). The
		// ":gender" part yields an ETextGender argument for |gender(...) in translations.
		FFormatArgumentValue NamedValue;
		if (UKzDialogueFunctionLibrary::TryResolveNamedArgument(this, ArgumentName, NamedValue))
		{
			CurrentLine.FormatArguments.Add(ArgumentName, NamedValue);
		}
	}
}

void UKzDialoguePlayer::ResolveSpeakingSpeaker()
{
	UKzDialogueSpeakerComponent* NewSpeaker = CurrentLine.Speaker.IsValid()
		? UKzDialogueSpeakerComponent::FindSpeaker(this, CurrentLine.Speaker.Asset)
		: nullptr;

	if (NewSpeaker != SpeakingSpeaker.Get())
	{
		// A different speaker takes over: close the previous one's mouth.
		if (UKzDialogueSpeakerComponent* Old = SpeakingSpeaker.Get())
		{
			Old->SetSpeakingLevel(0.0f);
		}
		SpeakingSpeaker = NewSpeaker;
	}

	// Effective speaking tuning for this line: the speaker's override if any, else the project defaults.
	ActiveSpeakingSettings = NewSpeaker
		? NewSpeaker->ResolveSpeakingSettings()
		: (UKzDialogueSettings::Get() ? UKzDialogueSettings::Get()->SpeakingDefaults : FKzSpeakingLevelSettings());
}

EKzLineAudioInterruptionPolicy UKzDialoguePlayer::ResolveAudioPolicy(const FKzDialogueLine& Line) const
{
	if (Line.AudioInterruptionPolicy != EKzLineAudioInterruptionPolicy::Inherit)
	{
		return Line.AudioInterruptionPolicy;
	}

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	if (!Settings) { return EKzLineAudioInterruptionPolicy::Stop; }

	if (const FKzDialogueChannelDefinition* ChannelDef = Settings->FindChannel(Channel))
	{
		if (ChannelDef->DefaultAudioInterruptionPolicy != EKzLineAudioInterruptionPolicy::Inherit)
		{
			return ChannelDef->DefaultAudioInterruptionPolicy;
		}
	}

	return Settings->DefaultAudioInterruptionPolicy != EKzLineAudioInterruptionPolicy::Inherit
		? Settings->DefaultAudioInterruptionPolicy
		: EKzLineAudioInterruptionPolicy::Stop;
}

EKzLineAudioSpatialization UKzDialoguePlayer::ResolveAudioSpatialization(const FKzDialogueLine& Line) const
{
	if (Line.AudioSpatialization != EKzLineAudioSpatialization::Inherit)
	{
		return Line.AudioSpatialization;
	}

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	if (!Settings) { return EKzLineAudioSpatialization::AttachedToSpeaker; }

	if (const FKzDialogueChannelDefinition* ChannelDef = Settings->FindChannel(Channel))
	{
		if (ChannelDef->DefaultAudioSpatialization != EKzLineAudioSpatialization::Inherit)
		{
			return ChannelDef->DefaultAudioSpatialization;
		}
	}

	// An Inherit smuggled into the project default via a hand-edited ini resolves as attached.
	return Settings->DefaultAudioSpatialization != EKzLineAudioSpatialization::Inherit
		? Settings->DefaultAudioSpatialization
		: EKzLineAudioSpatialization::AttachedToSpeaker;
}

void UKzDialoguePlayer::ResolveOutgoingAudio(const FKzDialogueLine& OutgoingLine, const FKzDialogueLine* IncomingLine)
{
	if (!IsValid(ActiveAudio)) { return; }

	const EKzLineAudioInterruptionPolicy Policy = ResolveAudioPolicy(OutgoingLine);

	bool bStop = true;
	switch (Policy)
	{
		case EKzLineAudioInterruptionPolicy::Continue:
			bStop = false;
			break;
		case EKzLineAudioInterruptionPolicy::ContinueIfDifferentSpeaker:
			bStop = !IncomingLine || IncomingLine->Speaker == OutgoingLine.Speaker;
			break;
		case EKzLineAudioInterruptionPolicy::Stop:
		default:
			break;
	}

	if (bStop)
	{
		StopLineAudio();
	}
	else
	{
		// Release: stop tracking ActiveAudio but keep a weak
		// ref so Abort/Interrupt can still hard-stop it.
		// The UAudioComponent has bAutoDestroy = true, so it
		// self-cleans when the sound finishes.
		ReleasedAudios.Add(ActiveAudio);
		ActiveAudio = nullptr;
	}
}

void UKzDialoguePlayer::StopReleasedAudios(float FadeTime)
{
	for (TWeakObjectPtr<UAudioComponent>& Weak : ReleasedAudios)
	{
		if (UAudioComponent* Audio = Weak.Get())
		{
			if (FadeTime > 0.0f) { Audio->FadeOut(FadeTime, 0.0f); }
			else { Audio->Stop(); }
		}
	}
	ReleasedAudios.Reset();
}

// ---------------------------------------------------------------------------------------
// Specific line bindings
// ---------------------------------------------------------------------------------------

TSet<FGuid> UKzDialoguePlayer::ResolveLineRefToMatchSet(const FKzDialogueLineRef& LineRef) const
{
	TSet<FGuid> Result;

	if (!LineRef.IsValid()) { return Result; }

	UKzDialogueAsset* Asset = LineRef.Asset.LoadSynchronous();
	if (!Asset) { return Result; }

	// If the GUID is a line, the match set is just that line.
	if (Asset->IndexOfLine(LineRef.LineId) != INDEX_NONE)
	{
		Result.Add(LineRef.LineId);
		return Result;
	}

	// Otherwise try as alias and expand to all referenced line ids.
	FKzDialogueAlias Alias;
	if (Asset->TryGetAliasById(LineRef.LineId, Alias))
	{
		Result.Reserve(Alias.LineIds.Num());
		for (const FGuid& Id : Alias.LineIds)
		{
			Result.Add(Id);
		}
	}

	return Result;
}

FGuid UKzDialoguePlayer::BindOnSpecificLineStarted(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind)
{
	if (!Callback.IsBound()) { return FGuid(); }

	TSet<FGuid> Matches = ResolveLineRefToMatchSet(LineRef);
	if (Matches.Num() == 0) { return FGuid(); }

	FSpecificLineBinding& Binding = SpecificLineStartedBindings.AddDefaulted_GetRef();
	Binding.Handle = FGuid::NewGuid();
	Binding.MatchingLineIds = MoveTemp(Matches);
	Binding.Callback = Callback;
	Binding.bAutoUnbind = bAutoUnbind;
	return Binding.Handle;
}

FGuid UKzDialoguePlayer::BindOnSpecificLineFinished(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind)
{
	if (!Callback.IsBound()) { return FGuid(); }

	TSet<FGuid> Matches = ResolveLineRefToMatchSet(LineRef);
	if (Matches.Num() == 0) { return FGuid(); }

	FSpecificLineBinding& Binding = SpecificLineFinishedBindings.AddDefaulted_GetRef();
	Binding.Handle = FGuid::NewGuid();
	Binding.MatchingLineIds = MoveTemp(Matches);
	Binding.Callback = Callback;
	Binding.bAutoUnbind = bAutoUnbind;
	return Binding.Handle;
}

void UKzDialoguePlayer::UnbindSpecificLine(FGuid BindingHandle)
{
	if (!BindingHandle.IsValid()) { return; }

	// Same handle can't be in both arrays (each Bind generates a fresh one), but we
	// don't track which one for simplicity. Removing by predicate is O(n) in both
	// arrays; binding count is expected to be small (handful per player).
	auto MatchesHandle = [BindingHandle](const FSpecificLineBinding& Binding) -> bool
		{
			return Binding.Handle == BindingHandle;
		};

	SpecificLineStartedBindings.RemoveAll(MatchesHandle);
	SpecificLineFinishedBindings.RemoveAll(MatchesHandle);
}

void UKzDialoguePlayer::DispatchSpecificLineEvent(TArray<FSpecificLineBinding>& Bindings, const FKzDialogueLine& Line)
{
	if (Bindings.Num() == 0) { return; }

	// A callback may bind or unbind specific-line bindings.
	// Snapshot the matching bindings before invoking any callback.
	struct FPendingDispatch
	{
		FKzOnDialogueLineSingleEvent Callback;
		FGuid Handle;
		bool bAutoUnbind;
	};

	TArray<FPendingDispatch, TInlineAllocator<4>> Pending;
	for (const FSpecificLineBinding& Binding : Bindings)
	{
		if (Binding.MatchingLineIds.Contains(Line.LineId))
		{
			Pending.Add({ Binding.Callback, Binding.Handle, Binding.bAutoUnbind });
		}
	}

	for (const FPendingDispatch& Dispatch : Pending)
	{
		Dispatch.Callback.ExecuteIfBound(this, Line);
	}

	// Remove auto-unbind entries still present (a callback may already have unbound itself).
	for (const FPendingDispatch& Dispatch : Pending)
	{
		if (Dispatch.bAutoUnbind)
		{
			const FGuid Handle = Dispatch.Handle;
			Bindings.RemoveAll([Handle](const FSpecificLineBinding& Binding) { return Binding.Handle == Handle; });
		}
	}
}

// ---------------------------------------------------------------------------------------
// Timeline evaluation
// ---------------------------------------------------------------------------------------

ETickableTickType UKzDialoguePlayer::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UKzDialoguePlayer::IsTickable() const
{
	if (SpeakingLevel > UE_KINDA_SMALL_NUMBER)
	{
		return true; // keep ticking to release the jaw toward 0
	}
	return !bPaused && State == EKzDialogueState::LinePlaying && (ActiveNotifies.Num() > 0 || IsValid(ActiveAudio));
}

TStatId UKzDialoguePlayer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKzDialoguePlayer, STATGROUP_Tickables);
}

void UKzDialoguePlayer::Tick(float DeltaTime)
{
	UpdateSpeakingLevel(DeltaTime);

	if (bPaused || State != EKzDialogueState::LinePlaying)
	{
		return;
	}

	TimelineClock += DeltaTime;

	// A notify may move the player on (Next, Stop, a new line on this channel). Flags are set before each
	// call so a flush in between pairs Begin with End, and the loop stops as soon as the state changed.
	const uint32 Serial = StateSerial;
	for (int32 Index = 0; Index < ActiveNotifies.Num(); ++Index)
	{
		FKzDialogueActiveNotify& Entry = ActiveNotifies[Index];
		if (Entry.bDone || !IsValid(Entry.Notify))
		{
			continue;
		}

		if (!Entry.bIsState)
		{
			if (TimelineClock >= Entry.Start)
			{
				Entry.bDone = true;
				if (UKzDialogueNotify* PointNotify = Cast<UKzDialogueNotify>(Entry.Notify))
				{
					PointNotify->Notify(BuildNotifyContext(Entry, TimelineClock, CachedLineDuration));
					if (Serial != StateSerial) { return; }
				}
			}
			continue;
		}

		UKzDialogueNotifyState* StateNotify = Cast<UKzDialogueNotifyState>(Entry.Notify);
		if (!StateNotify)
		{
			Entry.bDone = true;
			continue;
		}

		if (!Entry.bActive && TimelineClock >= Entry.Start)
		{
			Entry.bActive = true;
			StateNotify->NotifyBegin(BuildNotifyContext(Entry, TimelineClock, CachedLineDuration));
			if (Serial != StateSerial) { return; }
		}

		if (!Entry.bActive)
		{
			continue;
		}

		if (!Entry.bFlushBound && TimelineClock >= Entry.End)
		{
			Entry.bActive = false;
			Entry.bDone = true;
			StateNotify->NotifyEnd(BuildNotifyContext(Entry, TimelineClock, CachedLineDuration));
		}
		else
		{
			StateNotify->NotifyTick(BuildNotifyContext(Entry, TimelineClock, CachedLineDuration), DeltaTime);
		}
		if (Serial != StateSerial) { return; }
	}
}

void UKzDialoguePlayer::BakeTimeline()
{
	ActiveNotifies.Reset();
	TimelineClock = 0.0f;
	CachedLineDuration = 0.0f;

	UKzDialogueTimeline* Timeline = CurrentLine.Timeline;
	if (!Timeline || Timeline->IsEmpty())
	{
		return;
	}

	CachedLineDuration = ResolveLineDuration(CurrentLine);
	UKzDialogueSpeakerComponent* LineSpeaker = CurrentLine.Speaker.IsValid() ? UKzDialogueSpeakerComponent::FindSpeaker(this, CurrentLine.Speaker.Asset) : nullptr;

	FKzDialogueTimeResolveContext ResolveContext;
	ResolveContext.LineDuration = CachedLineDuration;
	// Synchronous load, not Get(): with AudioStartDelay the wave may not be in memory yet at
	// bake time, and anchor sources (audio markers) need it to resolve.
	ResolveContext.Audio = CurrentLine.Audio.IsNull() ? nullptr : CurrentLine.Audio.LoadSynchronous();

	for (const FKzDialogueNotifyTrack& Track : Timeline->Tracks)
	{
		for (const FKzDialogueNotifyEvent& Event : Track.Events)
		{
			if (!Event.bEnabled || !IsValid(Event.Notify))
			{
				continue;
			}

			const FKzDialogueTimeSource* Source = Event.TimeSource.GetPtr<FKzDialogueTimeSource>();
			if (!Source)
			{
				continue;
			}

			float Start = 0.0f;
			float End = 0.0f;
			Source->Resolve(ResolveContext, Start, End);

			FKzDialogueActiveNotify& Entry = ActiveNotifies.AddDefaulted_GetRef();
			Entry.Notify = Event.Notify;
			Event.Notify->SetOwningPlayer(this);
			Entry.LineSpeaker = LineSpeaker;
			Entry.bIsState = Event.Notify->IsA<UKzDialogueNotifyState>();
			Entry.Start = Start;
			Entry.End = FMath::Max(Start, End);
			Entry.bFlushBound = Entry.bIsState && (Entry.End >= CachedLineDuration - UE_KINDA_SMALL_NUMBER);
			Entry.bFireIfSkipped = Event.bFireIfSkipped;
		}
	}
}

void UKzDialoguePlayer::FlushTimeline()
{
	if (ActiveNotifies.Num() == 0)
	{
		return;
	}

	// Moved out first: an End may finish the dialogue or bake a new line, which must find an empty timeline.
	const TArray<FKzDialogueActiveNotify> Flushed = MoveTemp(ActiveNotifies);
	const float Clock = TimelineClock;
	const float LineDuration = CachedLineDuration;

	for (int32 Index = Flushed.Num() - 1; Index >= 0; --Index)
	{
		const FKzDialogueActiveNotify& Entry = Flushed[Index];
		if (Entry.bIsState && Entry.bActive && IsValid(Entry.Notify))
		{
			if (UKzDialogueNotifyState* StateNotify = Cast<UKzDialogueNotifyState>(Entry.Notify))
			{
				StateNotify->NotifyEnd(BuildNotifyContext(Entry, Clock, LineDuration));
			}
		}
	}

	// Skipped points that opted in.
	for (const FKzDialogueActiveNotify& Entry : Flushed)
	{
		if (!Entry.bIsState && !Entry.bDone && Entry.bFireIfSkipped && IsValid(Entry.Notify))
		{
			if (UKzDialogueNotify* PointNotify = Cast<UKzDialogueNotify>(Entry.Notify))
			{
				PointNotify->Notify(BuildNotifyContext(Entry, Clock, LineDuration));
			}
		}
	}
}

FKzDialogueNotifyContext UKzDialoguePlayer::BuildNotifyContext(const FKzDialogueActiveNotify& Entry, float Clock, float LineDuration)
{
	FKzDialogueNotifyContext Context;
	Context.Player = this;
	Context.LineSpeaker = Entry.LineSpeaker;

	UKzDialogueSpeakerComponent* Target = Entry.LineSpeaker;
	if (IsValid(Entry.Notify) && Entry.Notify->TargetSpeakerOverride)
	{
		Target = UKzDialogueSpeakerComponent::FindSpeaker(this, Entry.Notify->TargetSpeakerOverride);
	}

	Context.TargetSpeaker = Target;
	Context.TargetActor = Target ? Target->GetOwner() : nullptr;
	Context.LineDuration = LineDuration;
	Context.CurrentTime = Clock;
	Context.EventStart = Entry.Start;
	Context.EventEnd = Entry.End;
	return Context;
}