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
class UKzDialogueAsset;

namespace Kz::Tags::Dialogue
{
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MainChannel);
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BarkChannel);
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SystemChannel);
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
	FText GetDisplayLabel(int32 MaxTextLength = 0) const
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

/**
 * A randomized group of dialogue lines that share a speaker.
 *
 * When played, the asset picks one of the referenced LineIds at random. Useful for
 * variations of the same beat ("greet the player" with 4 different lines, "react to
 * surprise" with 3, etc.) without needing to wire a SoundCue with a Random selector
 * (which can't carry per-variant subtitles).
 *
 * Lines referenced by an alias must share the alias's Speaker. The validator will
 * surface mismatches.
 */
USTRUCT(BlueprintType)
struct FKzDialogueAlias
{
	GENERATED_BODY()

	/** Stable identifier for the alias. Survives renames so external references hold. */
	UPROPERTY(VisibleAnywhere, Category = "Dialogue|Alias")
	FGuid AliasId;

	/** Author-facing identifier, e.g. "Greeting", "Surprise", "Insult". Must be unique
	 *  within the asset. Used by gameplay code as a stable key. */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Alias")
	FName AliasName;

	/** Speaker constraint: every line referenced by this alias must use this speaker.
	 *  An empty/invalid speaker tag means the alias is for narration-style lines (no
	 *  speaker assigned). */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Alias")
	FKzDialogueSpeaker Speaker;

	/** Lines this alias resolves to. Picking the alias picks one at random. */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Alias")
	TArray<FGuid> LineIds;

	FKzDialogueAlias() = default;

	/** Returns a UI-facing label combining the alias name and its line count. */
	FText GetDisplayLabel() const
	{
		const FString NameStr = AliasName.IsNone() ? TEXT("(unnamed)") : AliasName.ToString();

		const FString SpeakerStr = Speaker.GetDisplayLabel().ToString();
		if (!SpeakerStr.IsEmpty())
		{
			return FText::Format(NSLOCTEXT("KzDialogueAlias", "AliasLabelWithSpeaker", "({0}) {1} [{2}]"),
				FText::FromString(SpeakerStr),
				FText::FromString(NameStr),
				FText::AsNumber(LineIds.Num()));
		}

		return FText::Format(NSLOCTEXT("KzDialogueAlias", "AliasLabel", "{0} [{1}]"),
			FText::FromString(NameStr),
			FText::AsNumber(LineIds.Num()));
	}
};

/**
 * Reference to either a single line or an alias inside a dialogue asset.
 *
 * Used by the picker, K2Node, Sequencer, and any future system that needs to point
 * at a "playable thing" inside a dialogue asset without committing to which kind it
 * is at the call site.
 */
USTRUCT(BlueprintType)
struct FKzDialogueAssetReference
{
	GENERATED_BODY()

	/** GUID of the line or alias. Combined with bIsAlias to disambiguate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FGuid Id;

	/** True if Id refers to an alias, false if it refers to a line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bIsAlias = false;

	bool IsValid() const { return Id.IsValid(); }

	bool operator==(const FKzDialogueAssetReference& Other) const
	{
		return Id == Other.Id && bIsAlias == Other.bIsAlias;
	}
};

/**
 * Reference to a single playable entry (line or alias) inside a dialogue asset.
 *
 * Stored as a soft pointer to the asset plus a GUID that the runtime resolves
 * against both the Lines and Aliases tables, so callers don't need to know whether
 * the GUID points to a concrete line or to an alias that randomizes one.
 *
 * Useful as a property in actors / data assets / structs whenever you want a
 * designer to pick a specific line from a specific asset without writing code.
 * The editor customization renders an asset picker plus a searchable dropdown.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueLineRef
{
	GENERATED_BODY()

	/**
	 * Asset that owns the line or alias. Soft so referencing this
	 * struct doesn't pull the asset into memory until it's needed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TSoftObjectPtr<UKzDialogueAsset> Asset;

	/** GUID of the line or alias inside Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FGuid LineId;

	bool IsValid() const { return !Asset.IsNull() && LineId.IsValid(); }

	/**
	 * Loads the asset (synchronously) and resolves the GUID to a concrete line.
	 * Returns false if the asset can't be loaded, the GUID is invalid, or it
	 * doesn't match any line or alias in the asset.
	 */
	bool TryResolve(FKzDialogueLine& OutLine) const;
};

/**
 * Per-channel configuration. Lives in UKzDialogueSettings and is consulted by the
 * subsystem on every Play() call to clamp priorities, decide interruption rules,
 * and (eventually) drive cross-channel ducking.
 */
USTRUCT(BlueprintType)
struct FKzDialogueChannelDefinition
{
	GENERATED_BODY()

	/** Tag identifying this channel (e.g. "Dialogue.Main"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Tag;

	/** Display name used in editor UI and debug overlays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	FText DisplayName;

	/** Priority used when neither the asset nor the caller specify one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	int32 DefaultPriority = 0;

	/**
	 * Lower bound for any priority on this channel. Caller priorities are clamped to
	 * this range, so a "Bark" channel can guarantee it never preempts story dialogue.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	int32 MinPriority = 0;

	/** Upper bound for any priority on this channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	int32 MaxPriority = 1000;

	/**
	 * When false, dialogues on this channel cannot be interrupted regardless of the
	 * incoming priority. Note: an asset's bInterruptible flag is also consulted; both
	 * must be true for an interruption to actually occur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bAllowInterruption = true;

	/**
	 * When this channel is active, other channels' audio is reduced by DuckedVolume.
	 * Implementation pending — flag is honored by the API but does not yet affect
	 * audio mixing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel|Ducking")
	bool bDuckOtherChannels = false;

	/** Volume multiplier applied to other channels while this one is playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel|Ducking", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float DuckedVolume = 0.3f;

	/** Fade time used when entering and leaving the ducked state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel|Ducking", meta = (ClampMin = 0.0))
	float DuckFadeTime = 0.2f;
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