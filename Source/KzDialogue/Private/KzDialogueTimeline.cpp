// Copyright 2026 kirzo

#include "KzDialogueTimeline.h"

#include "Sound/SoundWave.h"

void FKzDialogueTimeSource_Relative::Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const
{
	const float Scale = bNormalized ? Context.LineDuration : 1.0f;
	OutStart = Time * Scale;
	OutEnd = (Time + Duration) * Scale;
}

void FKzDialogueTimeSource_AudioMarker::Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const
{
	OutStart = FallbackTime;
	OutEnd = FallbackTime + Duration;

	const USoundWave* Wave = Cast<USoundWave>(Context.Audio);
	if (!Wave || MarkerName.IsNone())
	{
		return;
	}

	const float SampleRate = Wave->GetSampleRateForCurrentPlatform();
	if (SampleRate <= 0.0f)
	{
		return;
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
			return;
		}
	}
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