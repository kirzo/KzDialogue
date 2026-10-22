// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

class USoundBase;
struct FKzDialogueLine;

/**
 * The editor's dialogue audition, shared by every play button: one preview at a time, identified by a
 * key so each button can show whether it is the one playing. It only stops what it started itself.
 */
namespace KzDialogueAudition
{
	/** Plays Sound under Key, replacing the current audition: from Start, cut at End (0 = natural end). */
	void Play(USoundBase* Sound, const FString& Key, float Start = 0.0f, float End = 0.0f);

	/** Plays the line's audio range exactly as the game would, keyed by its LineId. */
	void PlayLine(const FKzDialogueLine& Line);

	/** Stops the audition, or only when it is the one playing Key. */
	void Stop(const FString& Key = FString());

	bool IsPlaying(const FString& Key);
	bool IsPlayingLine(const FGuid& LineId);

	/** Play-button behavior: stops the line when it is the one playing, plays it otherwise. */
	void ToggleLine(const FKzDialogueLine& Line);
}