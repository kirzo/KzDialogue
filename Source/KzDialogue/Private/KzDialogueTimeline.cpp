// Copyright 2026 kirzo

#include "KzDialogueTimeline.h"
#include "KzDialogue.h"

#include "Sound/SoundWave.h"

void FKzDialogueTimeSource_Relative::Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const
{
	const float Scale = bNormalized ? Context.LineDuration : 1.0f;
	OutStart = Time * Scale;
	OutEnd = (Time + Duration) * Scale;
}

void FKzDialogueTimeSource_AudioMarker::Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const
{
	if (FindMarker(Context.Audio, OutStart, OutEnd))
	{
		return;
	}

	OutStart = FallbackTime;
	OutEnd = FallbackTime + Duration;
	if (Context.bWarnOnFallback)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Audio marker '%s' not found in '%s'; using the fallback time (%.2f s)."), *MarkerName.ToString(), *GetNameSafe(Context.Audio), FallbackTime);
	}
}

bool FKzDialogueTimeSource_AudioMarker::FindMarker(const USoundBase* Audio, float& OutStart, float& OutEnd) const
{
	const USoundWave* Wave = Cast<USoundWave>(Audio);
	const float SampleRate = Wave ? Wave->GetSampleRateForCurrentPlatform() : 0.0f;
	if (SampleRate <= 0.0f || MarkerName.IsNone())
	{
		return false;
	}

	const FString MarkerLabel = MarkerName.ToString();
	for (const FSoundWaveCuePoint& Cue : Wave->GetCuePoints())
	{
		if (Cue.Label == MarkerLabel)
		{
			OutStart = static_cast<float>(Cue.FramePosition) / SampleRate;
			// A region marker carries its own length; an explicit Duration overrides it.
			const float RegionLength = static_cast<float>(Cue.FrameLength) / SampleRate;
			OutEnd = OutStart + (Duration > 0.0f ? Duration : RegionLength);
			return true;
		}
	}
	return false;
}

FKzDialogueNotifyEvent::FKzDialogueNotifyEvent()
{
	TimeSource.InitializeAs<FKzDialogueTimeSource_Relative>();
}

bool UKzDialogueTimeline::IsEmpty() const
{
	for (const FKzDialogueNotifyTrack& Track : Tracks)
	{
		if (Track.Events.Num() > 0)
		{
			return false;
		}
	}
	return true;
}