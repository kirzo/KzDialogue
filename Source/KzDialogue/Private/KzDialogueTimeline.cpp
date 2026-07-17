// Copyright 2026 kirzo

#include "KzDialogueTimeline.h"

void FKzDialogueTimeSource_Relative::Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const
{
	const float Scale = bNormalized ? Context.LineDuration : 1.0f;
	OutStart = Time * Scale;
	OutEnd = (Time + Duration) * Scale;
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