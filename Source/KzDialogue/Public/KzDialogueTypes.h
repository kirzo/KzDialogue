// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "InstancedStructContainer.h"
#include "StructUtils/PropertyBag.h"
#include "NativeGameplayTags.h"
#include "KzDialogueTypes.generated.h"

class USoundBase;

namespace Kz::Tags::Dialogue
{
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MainChannel);
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SpeakerBase);
}

/**
 * Runtime-resolvable identity of a speaker. Allows display name and audio attachment to
 * be decoupled from the line authoring; resolved against speaker components at play time.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueSpeaker
{
	GENERATED_BODY()

	/** Logical identity of the speaker (e.g. "Dialogue.Speaker.NPC.Kirzo"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker", meta = (Categories = "Dialogue.Speaker"))
	FGameplayTag SpeakerTag;

	/**
	 * Optional override for the display name.
	 * If empty, the speaker component is queried at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	FText DisplayNameOverride;

	bool IsValid() const { return SpeakerTag.IsValid() || !DisplayNameOverride.IsEmpty(); }

	/**
	 * Resolves a human-readable label for this speaker, suitable for editor UI and
	 * runtime fallbacks. Resolution order:
	 *   1. DisplayNameOverride if set.
	 *   2. The leaf of SpeakerTag (e.g. "Dialogue.Speaker.NPC.Kirzo" -> "Kirzo").
	 *   3. A "<Narration>" placeholder when neither is available.
	 */
	FText GetDisplayLabel() const
	{
		if (!DisplayNameOverride.IsEmpty())
		{
			return DisplayNameOverride;
		}

		if (SpeakerTag.IsValid())
		{
			return FText::FromName(SpeakerTag.GetTagLeafName());
		}

		return NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
	}
};

/** A single dialogue line. The smallest authorable unit in the system. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueLine
{
	GENERATED_BODY()

	/**
	 * Stable identifier. Auto-generated when the line is created.
	 * Used by the Sequencer section to reference a specific line,
	 * surviving reorders inside the asset.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dialogue|Line")
	FGuid LineId;

	/** Speaker for this line. May be empty (narration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line")
	FKzDialogueSpeaker Speaker;

	/** Localizable subtitle text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line", meta = (MultiLine = true))
	FText Text;

	/** Optional line audio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line")
	TSoftObjectPtr<USoundBase> Audio;

	/**
	 * Explicit duration in seconds.
	 * If <= 0, falls back to audio duration, then to the player's default duration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line", meta = (ClampMin = 0))
	float Duration = 0.0f;

	/** When true and Audio is set, the audio is spawned 2D regardless of speaker location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line")
	bool bPlayAudio2D = false;

	/** Free-form tags (mood, intent, channel hint, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line")
	FGameplayTagContainer Tags;

	/**
	 * Audio parameter overrides applied to the spawned UAudioComponent. Backed by
	 * FInstancedPropertyBag so any property type (bool, int, float, name, object,
	 * sound wave) can be stored without a custom variant.
	 */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line")
	FInstancedPropertyBag AudioParams;

	FKzDialogueLine() : LineId(FGuid::NewGuid()) {}

	bool IsValid() const { return !Text.IsEmpty() || !Audio.IsNull(); }

	/**
	 * Returns a "(Speaker) Text" formatted label for this line, suitable for editor
	 * UI, list previews, Sequencer section titles, etc.
	 */
	FText GetDisplayLabel(int32 MaxTextLength = 60) const
	{
		FString TextStr = Text.ToString();

		// Narration: drop the prefix so the label is just the text.
		if (!Speaker.IsValid())
		{
			if (MaxTextLength > 0 && TextStr.Len() > MaxTextLength)
			{
				TextStr = TextStr.Left(MaxTextLength) + TEXT("...");
			}
			return FText::FromString(TextStr);
		}

		FText SpeakerLabel = Speaker.GetDisplayLabel();

		if (MaxTextLength > 0)
		{
			// The prefix "({0}) " takes up length equal to the speaker's name + 3 characters
			const int32 PrefixLength = SpeakerLabel.ToString().Len() + 3;

			// Calculate how much space we actually have left for the dialogue text
			int32 AvailableLength = MaxTextLength - PrefixLength;

			// Safety check: ensure we leave at least a few characters for the text 
			// just in case the speaker name is unusually long
			AvailableLength = FMath::Max(AvailableLength, 5);

			if (TextStr.Len() > AvailableLength)
			{
				TextStr = TextStr.Left(AvailableLength) + TEXT("...");
			}
		}

		return FText::Format(INVTEXT("({0}) {1}"), SpeakerLabel, FText::FromString(TextStr));
	}
};

/** A choice presented to the player at a branching point. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
	FGameplayTagContainer Tags;

	/** Provider-specific payload (e.g. node id in a branching graph). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
	FName Payload;
};

/** High-level state of a dialogue player. */
UENUM(BlueprintType)
enum class EKzDialogueState : uint8
{
	/** No dialogue is being played. */
	Idle,
	/** Global enter (backdrop fade in, camera blend, etc.). */
	Entering,
	/** Per-line enter animation. */
	LineEntering,
	/** Line is fully visible and audio is playing. */
	LinePlaying,
	/** Per-line exit animation. */
	LineExiting,
	/** Global exit. */
	Exiting,
	/** Externally paused. Holds the previous state for resume. */
	Paused
};

/** Reason the dialogue ended. */
UENUM(BlueprintType)
enum class EKzDialogueFinishReason : uint8
{
	/** Reached the end of the provider's lines. */
	Completed,
	/** Stop() was called; ended with normal exit animation. */
	Stopped,
	/** Abort() was called; ended immediately without animation. */
	Aborted,
	/** Pre-empted by a higher-priority dialogue on the same channel. */
	Interrupted
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialogueStarted, class UKzDialoguePlayer*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueFinished, class UKzDialoguePlayer*, Player, EKzDialogueFinishReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueLineEvent, class UKzDialoguePlayer*, Player, const FKzDialogueLine&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialoguePlayerEvent, class UKzDialoguePlayer*, Player);