// Copyright 2026 kirzo

#include "KzDialogueTimeline.h"

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