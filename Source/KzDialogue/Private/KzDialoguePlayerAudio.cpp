// Copyright 2026 kirzo

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "Settings/KzDialogueSettings.h"
#include "KzDialogueSpeakerComponent.h"

#include "AudioDevice.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
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

void UKzDialoguePlayer::PrefetchLineAudio()
{
	// The new handle takes the current audio before the old one lets it go, so it never drops out of memory.
	TArray<FSoftObjectPath> Paths;
	if (!CurrentLine.Audio.IsNull()) { Paths.Add(CurrentLine.Audio.ToSoftObjectPath()); }
	const FKzDialogueLine NextLine = IsValid(Provider) ? Provider->PeekNext() : FKzDialogueLine{};
	if (!NextLine.Audio.IsNull()) { Paths.Add(NextLine.Audio.ToSoftObjectPath()); }

	AudioPrefetchHandle = Paths.IsEmpty() ? nullptr : UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths));
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
		// Release: stop tracking ActiveAudio but keep a weak ref so Abort/Interrupt can still hard-stop it.
		// The component auto-destroys when the sound finishes.
		ReleasedAudios.Add(ActiveAudio);

		// A pending AudioEndTime cut belongs to this take: it follows the released tail instead of cutting the next line's voice.
		UWorld* World = GetWorld();
		if (World && World->GetTimerManager().IsTimerActive(AudioStopTimerHandle))
		{
			const float Remaining = World->GetTimerManager().GetTimerRemaining(AudioStopTimerHandle);
			ClearTimer(AudioStopTimerHandle);
			FTimerHandle TailCut;
			World->GetTimerManager().SetTimer(TailCut, FTimerDelegate::CreateWeakLambda(ActiveAudio.Get(), [Tail = TWeakObjectPtr<UAudioComponent>(ActiveAudio)]()
			{
				if (UAudioComponent* Audio = Tail.Get()) { Audio->FadeOut(0.1f, 0.0f); }
			}), Remaining, false);
		}
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