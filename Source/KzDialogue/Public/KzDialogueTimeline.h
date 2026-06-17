// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzDialogueTimeline.generated.h"

class UKzDialogueNotifyBase;

/**
 * A single timed entry on a timeline track. Time/Duration are seconds, or a [0,1]
 * fraction of the line when bNormalized; both are baked to seconds at line start.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueNotifyEvent
{
	GENERATED_BODY()

	/** Event start. Seconds, or [0,1] of the line when bNormalized. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (ClampMin = 0))
	float Time = 0.0f;

	/** Window length for state notifies; 0 for point notifies. Seconds or fraction per bNormalized. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (ClampMin = 0))
	float Duration = 0.0f;

	/** When true, Time/Duration are a [0,1] fraction of the line; otherwise absolute seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bNormalized = false;

	/** When true, a point notify still fires if the line is skipped past its time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bFireIfSkipped = false;

	/** Notify executed for this event. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Event")
	TObjectPtr<UKzDialogueNotifyBase> Notify = nullptr;
};

/**
 * A named row of events. Rows are organizational (visual grouping); at runtime all
 * rows are flattened and evaluated by time.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueNotifyTrack
{
	GENERATED_BODY()

	/** Author-facing row label (e.g. "Animation", "Mood"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	FName Name;

	/** Events on this row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Track")
	TArray<FKzDialogueNotifyEvent> Events;
};

/**
 * Per-line timeline of notifies. Instanced and owned by a dialogue line; null when the
 * line has none. Owns the notify instances natively, so the line struct only carries a
 * single instanced pointer.
 */
UCLASS(EditInlineNew, DefaultToInstanced, BlueprintType)
class KZDIALOGUE_API UKzDialogueTimeline : public UObject
{
	GENERATED_BODY()

public:
	/** Named rows of notify events. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timeline")
	TArray<FKzDialogueNotifyTrack> Tracks;

	/** True when no track holds any event. */
	bool IsEmpty() const;
};