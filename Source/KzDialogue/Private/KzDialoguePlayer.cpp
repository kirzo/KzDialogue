// Copyright 2026 kirzo

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "Settings/KzDialogueSettings.h"
#include "KzDialogueAsset.h"
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
	// Route GetWorld through the outer chain. Subsystem-created players are outered to
	// the world, so this resolves correctly without any special handling.
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
	}
	Super::BeginDestroy();
}

// ---------------------------------------------------------------------------------------
// Public commands
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::Play(UKzDialogueProvider* InProvider)
{
	SetProvider(InProvider);
	StartDialogue();
}

void UKzDialoguePlayer::SetProvider(UKzDialogueProvider* InProvider)
{
	if (!IsValid(InProvider)) return;

	// Cleanly cancel anything currently playing before starting fresh.
	if (IsPlaying())
	{
		Abort();
	}

	Provider = InProvider;
	Provider->Reset();
}

void UKzDialoguePlayer::StartDialogue()
{
	// Only start if we have a provider and we are currently Idle
	if (!IsValid(Provider) || State != EKzDialogueState::Idle)
	{
		return;
	}

	OnDialogueStarted.Broadcast(this);
	Enter_Entering();
}

void UKzDialoguePlayer::Pause()
{
	if (State == EKzDialogueState::Idle || State == EKzDialogueState::Paused)
	{
		return;
	}

	StateBeforePause = State;
	State = EKzDialogueState::Paused;

	// Capture the time remaining on the line timer so we can resume cleanly.
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		if (TM.IsTimerActive(LineTimerHandle))
		{
			PausedTimeRemaining = TM.GetTimerRemaining(LineTimerHandle);
			TM.PauseTimer(LineTimerHandle);
		}
		if (TM.IsTimerActive(AudioDelayTimerHandle))
		{
			TM.PauseTimer(AudioDelayTimerHandle);
		}
		if (TM.IsTimerActive(LineStartDelayTimerHandle))
		{
			TM.PauseTimer(LineStartDelayTimerHandle);
		}
	}

	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(true);
	}

	OnPaused.Broadcast(this);
}

void UKzDialoguePlayer::Resume()
{
	if (State != EKzDialogueState::Paused)
	{
		return;
	}

	State = StateBeforePause;
	StateBeforePause = EKzDialogueState::Idle;

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		if (LineTimerHandle.IsValid())
		{
			TM.UnPauseTimer(LineTimerHandle);
		}
		if (AudioDelayTimerHandle.IsValid())
		{
			TM.UnPauseTimer(AudioDelayTimerHandle);
		}
		if (LineStartDelayTimerHandle.IsValid())
		{
			TM.UnPauseTimer(LineStartDelayTimerHandle);
		}
	}

	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(false);
	}

	OnResumed.Broadcast(this);
}

void UKzDialoguePlayer::Stop()
{
	if (State == EKzDialogueState::Idle)
	{
		return;
	}

	// From mid-line states, route through LineExiting -> Exiting (handled by transitions).
	if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LineTimerHandle);
		}
		Enter_LineExiting();
	}
	else if (State == EKzDialogueState::Entering)
	{
		// Skip the line phase entirely.
		Enter_Exiting();
	}
	// If already exiting, let it complete naturally.
}

void UKzDialoguePlayer::Abort()
{
	if (State == EKzDialogueState::Idle)
	{
		// Nothing to abort, but Continue-policy tails may outlive the dialogue: a hard stop on an
		// idle player means "silence the channel".
		StopReleasedAudios(0.0f);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
	}
	StopLineAudio(0.0f);
	StopReleasedAudios(0.0f);

	FinishWithReason(EKzDialogueFinishReason::Aborted);
}

void UKzDialoguePlayer::Interrupt()
{
	if (State == EKzDialogueState::Idle)
	{
		// Same as Abort: an interrupt on an idle player still silences ringing released tails.
		StopReleasedAudios(0.1f);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
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

void UKzDialoguePlayer::Skip()
{
	AdvanceCurrentLine();
}

void UKzDialoguePlayer::Next()
{
	AdvanceCurrentLine();
}

void UKzDialoguePlayer::AdvanceCurrentLine()
{
	if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LineTimerHandle);
		}
		Enter_LineExiting();
	}
}

// ---------------------------------------------------------------------------------------
// View notifications
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::NotifyEnterFinished()
{
	// Ignore a notification for a phase we already left (stale fade): also avoids decrementing the next phase.
	if (State != EKzDialogueState::Entering) { return; }
	if (--PendingViewAcks <= 0) { Enter_LineEntering(); }
}

void UKzDialoguePlayer::NotifyExitFinished()
{
	if (State != EKzDialogueState::Exiting) { return; }
	if (--PendingViewAcks <= 0) { FinishWithReason(EKzDialogueFinishReason::Completed); }
}

void UKzDialoguePlayer::NotifyLineEnterFinished()
{
	if (State != EKzDialogueState::LineEntering) { return; }
	if (--PendingViewAcks <= 0) { Enter_LinePlaying(); }
}

void UKzDialoguePlayer::NotifyLineExitFinished()
{
	if (State != EKzDialogueState::LineExiting) { return; }
	if (--PendingViewAcks <= 0) { AdvanceAfterLineExit(); }
}

void UKzDialoguePlayer::AdvanceAfterLineExit()
{
	OnLineFinished.Broadcast(this, CurrentLine);

	// Decide whether to play another line or wrap up.
	if (IsValid(Provider) && Provider->HasNext())
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

void UKzDialoguePlayer::Enter_Entering()
{
	State = EKzDialogueState::Entering;
	PendingViewAcks = 0;
	OnRequestDialogueEnter.Broadcast(this);

	// No bound view claimed an enter animation: skip to the first line immediately.
	if (State == EKzDialogueState::Entering && PendingViewAcks == 0)
	{
		Enter_LineEntering();
	}
}

void UKzDialoguePlayer::Enter_LineEntering()
{
	if (!IsValid(Provider) || !Provider->HasNext())
	{
		Enter_Exiting();
		return;
	}

	State = EKzDialogueState::LineEntering;

	const FKzDialogueLine PreviousLine = CurrentLine;
	CurrentLine = Provider->Advance();

	// Decide what to do with the previous line's audio now that we know the incoming one.
	ResolveOutgoingAudio(PreviousLine, &CurrentLine);

	// Fill ambient template arguments before anyone sees the line, so every event and view
	// receives a copy that formats correctly.
	ResolveLineFormatArguments();

	// Silent pre-line gap: nothing presents (no subtitle, no audio, no view enter) until it
	// elapses, then the whole line appears at once. Scaled by TimeScale like every timing.
	// Clearing first also kills any stale gap timer from an aborted previous dialogue.
	const float Gap = FMath::Max(0.0f, CurrentLine.LineStartDelay) / FMath::Max(0.1f, TimeScale);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineStartDelayTimerHandle);
		if (Gap > UE_KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(LineStartDelayTimerHandle, this, &UKzDialoguePlayer::HandleLineStartDelayElapsed, Gap, /*loop=*/false);
			return;
		}
	}

	PresentLine();
}

void UKzDialoguePlayer::HandleLineStartDelayElapsed()
{
	if (State == EKzDialogueState::LineEntering)
	{
		PresentLine();
	}
}

void UKzDialoguePlayer::PresentLine()
{
	OnLineStarted.Broadcast(this, CurrentLine);
	DispatchSpecificLineEvent(SpecificLineStartedBindings, CurrentLine);

	if (bStartAudioOnLineEnter)
	{
		StartLineAudio();
	}

	PendingViewAcks = 0;
	OnRequestLineEnter.Broadcast(this, CurrentLine);

	if (State == EKzDialogueState::LineEntering && PendingViewAcks == 0)
	{
		Enter_LinePlaying();
	}
}

void UKzDialoguePlayer::Enter_LinePlaying()
{
	State = EKzDialogueState::LinePlaying;

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

	const float Duration = (PausedTimeRemaining > 0.0f) ? PausedTimeRemaining : ResolveLineDuration(CurrentLine);
	PausedTimeRemaining = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LineTimerHandle, this, &UKzDialoguePlayer::HandleLineTimerElapsed, Duration, /*loop=*/false);
	}
	else
	{
		// No world (unlikely in normal use): advance immediately.
		HandleLineTimerElapsed();
	}
}

void UKzDialoguePlayer::Enter_LineExiting()
{
	State = EKzDialogueState::LineExiting;

	// The line is ending: audio still waiting on its AudioStartDelay must never start now,
	// and a pending pre-line gap (stopped mid-gap) must never present.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AudioDelayTimerHandle);
		World->GetTimerManager().ClearTimer(LineStartDelayTimerHandle);
	}

	FlushTimeline();
	PendingViewAcks = 0;
	OnRequestLineExit.Broadcast(this, CurrentLine);
	DispatchSpecificLineEvent(SpecificLineFinishedBindings, CurrentLine);

	if (State == EKzDialogueState::LineExiting && PendingViewAcks == 0)
	{
		AdvanceAfterLineExit();
	}
}

void UKzDialoguePlayer::Enter_Exiting()
{
	State = EKzDialogueState::Exiting;
	ResolveOutgoingAudio(CurrentLine, nullptr);
	PendingViewAcks = 0;
	OnRequestDialogueExit.Broadcast(this);

	if (State == EKzDialogueState::Exiting && PendingViewAcks == 0)
	{
		FinishWithReason(EKzDialogueFinishReason::Completed);
	}
}

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::HandleLineTimerElapsed()
{
	if (State == EKzDialogueState::LinePlaying)
	{
		Enter_LineExiting();
	}
}

void UKzDialoguePlayer::FinishWithReason(EKzDialogueFinishReason Reason)
{
	FlushTimeline();

	UKzDialogueProvider* PreviousProvider = Provider;

	State = EKzDialogueState::Idle;
	StateBeforePause = EKzDialogueState::Idle;
	PausedTimeRemaining = 0.0f;
	Provider = nullptr;
	CurrentLine = FKzDialogueLine{};

	OnDialogueFinished.Broadcast(this, Reason);

	// We deliberately keep PreviousProvider alive only inside this scope; it'll be GC'd
	// when its outer goes out of scope.
	(void)PreviousProvider;
}

float UKzDialoguePlayer::ResolveLineDuration(const FKzDialogueLine& Line) const
{
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const float Fallback = Settings ? Settings->DefaultDuration : 2.5f;

	float AudioLength = 0.0f;
	if (const USoundBase* Sound = Line.Audio.Get())
	{
		AudioLength = Sound->GetDuration();
	}

	// The audio lead extends the line: the text lives its resolved time plus the delay.
	const float Duration = Line.ResolveDuration(AudioLength, Fallback) + FMath::Max(0.0f, Line.AudioStartDelay);

	// Apply TimeScale, but clamp to a sane minimum so we never set a 0-duration timer.
	const float Scaled = Duration / FMath::Max(0.1f, TimeScale);
	return FMath::Max(0.1f, Scaled);
}

void UKzDialoguePlayer::StartLineAudio()
{
	// Subtitles-lead-the-voice: defer the whole audio start (creation + envelope bind + play) by
	// the line's AudioStartDelay. TimeScale compresses the lead like every other line timing.
	const float Delay = FMath::Max(0.0f, CurrentLine.AudioStartDelay) / FMath::Max(0.1f, TimeScale);
	if (Delay > UE_KINDA_SMALL_NUMBER && !CurrentLine.Audio.IsNull())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(AudioDelayTimerHandle, this, &UKzDialoguePlayer::HandleAudioDelayElapsed, Delay, /*loop=*/false);
			return;
		}
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

	// Apply audio params from the property bag. Each value is forwarded to the audio
	// component using the type-appropriate setter.
	const FInstancedPropertyBag& Bag = CurrentLine.AudioParams;
	if (const UPropertyBag* Desc = Bag.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Prop : Desc->GetPropertyDescs())
		{
			const FName ParamName = Prop.Name;
			if (ParamName.IsNone()) { continue; }

			switch (Prop.ValueType)
			{
			case EPropertyBagPropertyType::Bool:
			{
				if (auto Result = Bag.GetValueBool(ParamName); Result.HasValue())
				{
					ActiveAudio->SetBoolParameter(ParamName, Result.GetValue());
				}
				break;
			}
			case EPropertyBagPropertyType::Int32:
			{
				if (auto Result = Bag.GetValueInt32(ParamName); Result.HasValue())
				{
					ActiveAudio->SetIntParameter(ParamName, Result.GetValue());
				}
				break;
			}
			case EPropertyBagPropertyType::Float:
			case EPropertyBagPropertyType::Double:
			{
				if (auto Result = Bag.GetValueDouble(ParamName); Result.HasValue())
				{
					ActiveAudio->SetFloatParameter(ParamName, static_cast<float>(Result.GetValue()));
				}
				break;
			}
			case EPropertyBagPropertyType::Name:
			{
				if (auto Result = Bag.GetValueName(ParamName); Result.HasValue())
				{
					ActiveAudio->SetStringParameter(ParamName, Result.GetValue().ToString());
				}
				break;
			}
			case EPropertyBagPropertyType::Object:
			{
				if (auto Result = Bag.GetValueObject(ParamName); Result.HasValue())
				{
					if (USoundWave* Wave = Cast<USoundWave>(Result.GetValue()))
					{
						ActiveAudio->SetWaveParameter(ParamName, Wave);
					}
					else
					{
						ActiveAudio->SetObjectParameter(ParamName, Result.GetValue());
					}
				}
				break;
			}
			default:
				break;
			}
		}
	}

	// Bind BEFORE Play: the engine captures whether to run the envelope follower (bUpdateSingleEnvelopeValue)
	// from OnAudioSingleEnvelopeValue.IsBound() at sound-start time, so binding after Play never fires it.
	BindAudioEnvelope();
	ResolveSpeakingSpeaker(); // resolve speaker + tuning before the first envelope callback can land
	ActiveAudio->Play();
}

void UKzDialoguePlayer::StopLineAudio(float FadeTime)
{
	// Covers hard teardowns (Abort / Interrupt / destroy): cancel a pending deferred start too.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AudioDelayTimerHandle);
	}

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
	return State == EKzDialogueState::LinePlaying && (ActiveNotifies.Num() > 0 || IsValid(ActiveAudio));
}

TStatId UKzDialoguePlayer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKzDialoguePlayer, STATGROUP_Tickables);
}

void UKzDialoguePlayer::Tick(float DeltaTime)
{
	UpdateSpeakingLevel(DeltaTime);

	if (State != EKzDialogueState::LinePlaying)
	{
		return;
	}

	TimelineClock += DeltaTime;

	for (FKzDialogueActiveNotify& Entry : ActiveNotifies)
	{
		if (Entry.bDone || !IsValid(Entry.Notify))
		{
			continue;
		}

		if (Entry.bIsState)
		{
			UKzDialogueNotifyState* StateNotify = Cast<UKzDialogueNotifyState>(Entry.Notify);
			if (!StateNotify)
			{
				Entry.bDone = true;
				continue;
			}

			if (!Entry.bActive && TimelineClock >= Entry.Start)
			{
				StateNotify->NotifyBegin(BuildNotifyContext(Entry));
				Entry.bActive = true;
			}

			if (Entry.bActive)
			{
				if (!Entry.bFlushBound && TimelineClock >= Entry.End)
				{
					StateNotify->NotifyEnd(BuildNotifyContext(Entry));
					Entry.bActive = false;
					Entry.bDone = true;
				}
				else
				{
					StateNotify->NotifyTick(BuildNotifyContext(Entry), DeltaTime);
				}
			}
		}
		else if (TimelineClock >= Entry.Start)
		{
			if (UKzDialogueNotify* PointNotify = Cast<UKzDialogueNotify>(Entry.Notify))
			{
				PointNotify->Notify(BuildNotifyContext(Entry));
			}
			Entry.bDone = true;
		}
	}
}

void UKzDialoguePlayer::BakeTimeline()
{
	ActiveNotifies.Reset();
	TimelineClock = 0.0f;
	CachedLineSpeaker = nullptr;
	CachedLineDuration = 0.0f;

	UKzDialogueTimeline* Timeline = CurrentLine.Timeline;
	if (!Timeline || Timeline->IsEmpty())
	{
		return;
	}

	CachedLineDuration = ResolveLineDuration(CurrentLine);
	if (CurrentLine.Speaker.IsValid())
	{
		CachedLineSpeaker = UKzDialogueSpeakerComponent::FindSpeaker(this, CurrentLine.Speaker.Asset);
	}

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

	// End active states in reverse (LIFO).
	for (int32 Index = ActiveNotifies.Num() - 1; Index >= 0; --Index)
	{
		FKzDialogueActiveNotify& Entry = ActiveNotifies[Index];
		if (Entry.bIsState && Entry.bActive && IsValid(Entry.Notify))
		{
			if (UKzDialogueNotifyState* StateNotify = Cast<UKzDialogueNotifyState>(Entry.Notify))
			{
				StateNotify->NotifyEnd(BuildNotifyContext(Entry));
			}
			Entry.bActive = false;
		}
	}

	// Fire skipped points that opted in.
	for (FKzDialogueActiveNotify& Entry : ActiveNotifies)
	{
		if (!Entry.bIsState && !Entry.bDone && Entry.bFireIfSkipped && IsValid(Entry.Notify))
		{
			if (UKzDialogueNotify* PointNotify = Cast<UKzDialogueNotify>(Entry.Notify))
			{
				PointNotify->Notify(BuildNotifyContext(Entry));
			}
		}
	}

	ActiveNotifies.Reset();
	CachedLineSpeaker = nullptr;
}

FKzDialogueNotifyContext UKzDialoguePlayer::BuildNotifyContext(const FKzDialogueActiveNotify& Entry)
{
	FKzDialogueNotifyContext Context;
	Context.Player = this;
	Context.LineSpeaker = CachedLineSpeaker;

	UKzDialogueSpeakerComponent* Target = CachedLineSpeaker;
	if (IsValid(Entry.Notify) && Entry.Notify->TargetSpeakerOverride)
	{
		Target = UKzDialogueSpeakerComponent::FindSpeaker(this, Entry.Notify->TargetSpeakerOverride);
	}

	Context.TargetSpeaker = Target;
	Context.TargetActor = Target ? Target->GetOwner() : nullptr;
	Context.LineDuration = CachedLineDuration;
	Context.CurrentTime = TimelineClock;
	Context.EventStart = Entry.Start;
	Context.EventEnd = Entry.End;
	return Context;
}