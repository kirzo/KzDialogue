// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "KzDialogueTimeline.generated.h"

class USoundBase;
class UKzDialogueNotifyBase;

/** Inputs available when baking an event's time source to seconds at line start. */
struct FKzDialogueTimeResolveContext
{
	/** Resolved line duration, in seconds. */
	float LineDuration = 0.0f;

	/** Audio currently loaded for the line (for anchor sources). May be null. */
	const USoundBase* Audio = nullptr;
};

/**
 * Base for an event's timing. Resolved to absolute seconds [Start, End] at line start.
 * Relative timing survives localization; anchor-based sources (e.g. audio cue points)
 * can be added later without touching the evaluator.
 */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueTimeSource
{
	GENERATED_BODY()

	virtual ~FKzDialogueTimeSource() = default;

	/** Bake to absolute seconds for the playing line. End == Start means instantaneous. */
	virtual void Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const { OutStart = 0.0f; OutEnd = 0.0f; }
};

/**
 * Time relative to the line: absolute seconds, or a [0,1] fraction of the line duration
 * when bNormalized. Language-agnostic, since it scales to each language's duration.
 */
USTRUCT(meta = (DisplayName = "Relative"))
struct KZDIALOGUE_API FKzDialogueTimeSource_Relative : public FKzDialogueTimeSource
{
	GENERATED_BODY()

	/** Event start. Seconds, or [0,1] of the line when bNormalized. */
	UPROPERTY(EditAnywhere, Category = "Time", meta = (ClampMin = 0))
	float Time = 0.0f;

	/** Window length for state notifies; 0 for point notifies. Seconds or fraction per bNormalized. */
	UPROPERTY(EditAnywhere, Category = "Time", meta = (ClampMin = 0))
	float Duration = 0.0f;

	/** When true, Time/Duration are a [0,1] fraction of the line; otherwise absolute seconds. */
	UPROPERTY(EditAnywhere, Category = "Time")
	bool bNormalized = false;

	virtual void Resolve(const FKzDialogueTimeResolveContext& Context, float& OutStart, float& OutEnd) const override;
};

/**
 * A single timed entry on a timeline track. TimeSource resolves to absolute seconds at
 * line start; the notify's class decides whether the window or just the start is used.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueNotifyEvent
{
	GENERATED_BODY()

	FKzDialogueNotifyEvent();

	/** How this event's timing resolves to seconds. Relative by default; pluggable (e.g. an audio anchor) later. */
	UPROPERTY(EditAnywhere, Category = "Event", meta = (BaseStruct = "/Script/KzDialogue.KzDialogueTimeSource", ExcludeBaseStruct))
	FInstancedStruct TimeSource;

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