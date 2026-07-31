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
class UKzDialogueTimeline;
class UKzSpeakerAsset;

namespace Kz::Tags::Dialogue
{
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MainChannel);
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BarkChannel);
	KZDIALOGUE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SystemChannel);
}

/**
 * Reference to the character speaking a line or alias. The asset reference itself IS the
 * speaker identity: registry lookups, equality checks and editor filters compare it
 * directly. Null = narration.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueSpeaker
{
	GENERATED_BODY()

	/** Character speaking. Null = narration. DisplayName keeps flattened rows (ShowOnlyInnerProperties hosts) reading "Speaker". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker", meta = (DisplayName = "Speaker"))
	TObjectPtr<UKzSpeakerAsset> Asset = nullptr;

	bool IsValid() const { return Asset != nullptr; }
	bool operator==(const FKzDialogueSpeaker& Other) const { return Asset == Other.Asset; }

	/** Human-readable label: the asset's resolved name, its asset name as editor fallback, or "<Narration>" when null. */
	FText GetDisplayLabel() const;
};

/**
 * Policy for what happens to a line's audio when the player transitions
 * to the next line.
 * Resolves in cascade: line override -> channel default -> player default.
 */
UENUM(BlueprintType)
enum class EKzLineAudioInterruptionPolicy : uint8
{
	/** Use the value from the next layer up. Only valid as an override. */
	Inherit,

	/** Stop the audio when the line transitions away. */
	Stop,

	/** Let the audio finish if the incoming line has a different speaker; stop otherwise. */
	ContinueIfDifferentSpeaker,

	/** Always let the audio finish; subsequent lines' audios overlap. */
	Continue
};

/** How a line's audio is placed in the world. Resolves in cascade: line override -> channel default -> project default. */
UENUM(BlueprintType)
enum class EKzLineAudioSpatialization : uint8
{
	/** Use the value from the next layer up. Only valid as an override. */
	Inherit,

	/** Non-spatialized (UI-style) audio. */
	TwoD UMETA(DisplayName = "2D"),

	/** Attached to the resolved speaker component's actor; follows it. Falls back to 2D when the line has no speaker or no component is registered in the world. */
	AttachedToSpeaker
};

/** How a line's playback length is derived from its Duration field and its audio. */
UENUM(BlueprintType)
enum class EKzLineDurationMode : uint8
{
	/** Duration if > 0, else the audio's length, else the project default (legacy behavior). */
	Auto,
	/** Use Duration as the line's length, ignoring the audio. */
	Override,
	/** Line length = the audio's length + Duration, to extend a voiced line. */
	ExtendAudio
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line", meta = (ShowOnlyInnerProperties))
	FKzDialogueSpeaker Speaker;

	/** Localizable subtitle text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line", meta = (MultiLine = true))
	FText Text;

	/**
	 * Optional line audio. Localized VO uses UE's built-in asset localization: place the
	 * per-culture variant under Content/L10N/<culture>/ mirroring this asset's path, and the
	 * soft pointer resolves to the active culture's package automatically. No per-culture
	 * mapping is stored on the line.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Audio")
	TSoftObjectPtr<USoundBase> Audio;

	/**
	 * Seconds between the line's start and its audio starting, so subtitles can lead the voice.
	 * Added on top of the line's resolved duration, and scaled by the player's TimeScale like
	 * every other line timing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Audio", meta = (ClampMin = 0))
	float AudioStartDelay = 0.0f;

	/**
	 * Duration value in seconds. How it is used depends on DurationMode: as the whole length, as
	 * an extension on top of the audio, or as the explicit length when > 0 (Auto).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Timing", meta = (ClampMin = 0))
	float Duration = 0.0f;

	/** How the line's playback length is derived from Duration and the audio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Timing")
	EKzLineDurationMode DurationMode = EKzLineDurationMode::Auto;

	/**
	 * Seconds of silent gap before the line presents: nothing shows and nothing sounds, then
	 * subtitle and audio appear together. AudioStartDelay composes on top, inside the line
	 * (the subtitle may still lead the voice). Extends the line's total time and is scaled
	 * by the player's TimeScale like every other line timing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Timing", meta = (ClampMin = 0))
	float LineStartDelay = 0.0f;

	/** How this line's audio is placed in the world. Resolution chain: line > channel > project settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Audio")
	EKzLineAudioSpatialization AudioSpatialization = EKzLineAudioSpatialization::Inherit;

	/** Policy applied to this line's audio when the player transitions to the next line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Audio")
	EKzLineAudioInterruptionPolicy AudioInterruptionPolicy = EKzLineAudioInterruptionPolicy::Inherit;

	/**
	 * Channel this line plays on when the callsite doesn't pass one explicitly.
	 * Resolution chain: callsite > line > audio SoundClass mapping > asset > project settings.
	 * For alias entries the line level only applies when every line of the alias agrees on the
	 * same DefaultChannel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Playback", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/** Free-form tags (mood, intent, channel hint, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Playback")
	FGameplayTagContainer Tags;

	/**
	 * Audio parameter overrides applied to the spawned UAudioComponent. Backed by
	 * FInstancedPropertyBag so any property type (bool, int, float, name, object,
	 * sound wave) can be stored without a custom variant.
	 */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Line|Audio")
	FInstancedPropertyBag AudioParams;

	/**
	 * Named arguments for a templated Text ("{Count}", "{PlayerName}"), applied by views at
	 * display time via GetFormattedText. Runtime data set by the call site on its line copy
	 * (see UKzDialogueFunctionLibrary::SetLine*Argument), never authored or serialized; empty
	 * for the common static line. Plural/gender grammar lives in the (translated) pattern
	 * itself via FText's ICU modifiers, so each culture picks its own forms.
	 */
	FFormatNamedArguments FormatArguments;

	/**
	 * Per-line timeline, owned by the asset (UKzDialogueAsset::Timelines, keyed by LineId) and
	 * filled in whenever the line is resolved from its asset (TryGetLineById and the asset
	 * provider). Transient and not authored on the line, so a line built by hand has none.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Dialogue|Line")
	TObjectPtr<UKzDialogueTimeline> Timeline = nullptr;
	
#if WITH_EDITORONLY_DATA
	/** Director notes for the translator: tone, intent, audience. Not localizable; exported alongside the line in the translation flow, never shipped. */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Line|Localization", meta = (MultiLine = true))
	FString TranslatorNotes;

	/** Maximum characters a translation of Text may use (fixed-width subtitles, bark bubbles). 0 = no limit. Consumed by validators and the export flow, not at runtime. */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Line|Localization", meta = (ClampMin = 0))
	int32 MaxCharacters = 0;

	/** Uncheck when this line's audio is deliberately NOT localized: it stops counting as pending audio work per culture and exports flag it so the studio knows. The source take's recording state is still tracked. */
	UPROPERTY(EditAnywhere, Category = "Dialogue|Line|Audio")
	bool bLocalizeAudio = true;
#endif

	/** CRC32 of Text's source string. Updated automatically. Used to detect drift between authored text and existing translations. */
	UPROPERTY()
	uint32 SourceTextHash = 0;

	/** CRC32 of Text's source string at the moment Audio was last assigned. Compared against SourceTextHash to flag lines whose text changed after their audio was recorded. 0 = no audio. */
	UPROPERTY()
	uint32 RecordedTextHash = 0;

	FKzDialogueLine() : LineId(FGuid::NewGuid()) {}

	bool IsValid() const { return !Text.IsEmpty() || !Audio.IsNull(); }

	/** Text with FormatArguments applied; Text as-is when there are none (the common case). */
	FText GetFormattedText() const;

	/**
	 * Effective playback length in seconds (before TimeScale) from DurationMode, Duration, the
	 * resolved audio length (0 if none) and the project default. Callers resolve the audio length.
	 */
	float ResolveDuration(float AudioLength, float DefaultDuration) const
	{
		switch (DurationMode)
		{
		case EKzLineDurationMode::Override:
			return Duration;
		case EKzLineDurationMode::ExtendAudio:
			return AudioLength + Duration;
		default:
			return Duration > 0.0f ? Duration : (AudioLength > 0.0f ? AudioLength : DefaultDuration);
		}
	}

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

/** Selection policy used to pick a line when an alias is resolved at runtime. */
UENUM(BlueprintType)
enum class EKzAliasSelectionMode : uint8
{
	/** Uniform random over all lines. May repeat the same line back-to-back. */
	Random,

	/**
	 * Uniform random, but never picks the same line twice in a row. With N=2 it
	 * oscillates strictly. Equivalent to Random when there's only one line.
	 */
	RandomNoRepeat,

	/**
	 * Shuffles all lines, plays each one once in shuffle order, then reshuffles.
	 * When reshuffling, ensures the first line of the new bag isn't the last line
	 * of the previous bag (no perceived repeat across bag boundaries).
	 */
	ShuffleBag,

	/**
	 * Plays lines in the order declared in the alias. Loops back to the first
	 * line after the last one.
	 */
	Sequential
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
	UPROPERTY(VisibleAnywhere, Category = "Dialogue")
	FGuid AliasId;

	/**
	 * Author-facing identifier, e.g. "Greeting", "Surprise", "Insult".
	 * Must be unique within the asset. Used by gameplay code as a stable key.
	 */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FName AliasName;

	/**
	 * Speaker constraint: every line referenced by this alias must use this speaker.
	 * A null speaker means the alias is for narration-style lines (no speaker assigned).
	 */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ShowOnlyInnerProperties))
	FKzDialogueSpeaker Speaker;

	/** How the subsystem picks a line each time the alias is resolved. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	EKzAliasSelectionMode SelectionMode = EKzAliasSelectionMode::ShuffleBag;

	/**
	 * Channel this alias plays on when the callsite doesn't pass one explicitly.
	 * If empty, the alias falls back to its lines' channel when ALL of them agree on the
	 * same one, then to the asset's, then to project settings.
	 */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/**
	 * Minimum seconds that must elapse between successful resolutions of this alias.
	 * If the alias is resolved within this window, the resolution is rejected (returns
	 * an invalid GUID, callers get a null player). Useful for "bark cooldowns" — three
	 * variations of "ouch!" that shouldn't fire back-to-back even if the gameplay code
	 * triggers them rapidly.
	 *
	 * 0 = no cooldown (default).
	 */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ClampMin = 0, Units = "Seconds"))
	float CooldownSeconds = 0.0f;

	/** Lines this alias resolves to. Picking the alias picks one at random. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TArray<FGuid> LineIds;

	FKzDialogueAlias() = default;

	/** Returns a UI-facing label combining the alias name and its line count. */
	FText GetDisplayLabel() const
	{
		const FString NameStr = AliasName.IsNone() ? TEXT("(unnamed)") : AliasName.ToString();

		if (Speaker.IsValid())
		{
			return FText::Format(NSLOCTEXT("KzDialogueAlias", "AliasLabelWithSpeaker", "({0}) {1} [{2}]"),
				Speaker.GetDisplayLabel(),
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
 * Reference to a list of playable entries (lines or aliases) inside a single dialogue
 * asset. Equivalent to a TArray<FKzDialogueLineRef> where every element shares the
 * same Asset, with a stricter shape: one asset reference, many ids.
 *
 * Useful for "a sequence of related lines" — barks, intros, multi-step events —
 * where forcing the designer to repeat the asset per element is noise.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueLineList
{
	GENERATED_BODY()

	/**
	 * Asset that owns all entries in this list. Soft so referencing this
	 * struct doesn't pull the asset into memory until it's needed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TSoftObjectPtr<UKzDialogueAsset> Asset;

	/** GUIDs of lines or aliases inside Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FGuid> LineIds;

	bool IsValid() const { return !Asset.IsNull() && LineIds.Num() > 0; }
	int32 Num() const { return LineIds.Num(); }

	/**
	 * Resolve a single entry by index. Returns false if the index is out of range,
	 * the asset can't be loaded, or the GUID doesn't match anything in the asset.
	 */
	bool TryResolve(int32 Index, FKzDialogueLine& OutLine) const;

	/**
	 * Resolve every entry. The output mirrors LineIds order; entries that fail to
	 * resolve are skipped (so the output may be shorter than LineIds.Num()).
	 * Returns true if at least one entry was resolved.
	 */
	bool TryResolveAll(TArray<FKzDialogueLine>& OutLines) const;

	/**
	 * Convert this list into individual FKzDialogueLineRef instances, each
	 * sharing this list's Asset. The output mirrors LineIds order one-to-one.
	 */
	void GetLineRefs(TArray<FKzDialogueLineRef>& OutRefs) const;
};

/**
 * Per-channel configuration. Lives in UKzDialogueSettings and is consulted by the
 * subsystem on every Play() call to decide default priorities, interruption rules,
 * and (eventually) cross-channel ducking.
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
	 * When false, dialogues on this channel cannot be interrupted regardless of the
	 * incoming priority. Note: an asset's bInterruptible flag is also consulted; both
	 * must be true for an interruption to actually occur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bAllowInterruption = true;

	/** Default audio interruption policy for lines on this channel. Lines may override per-line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	EKzLineAudioInterruptionPolicy DefaultAudioInterruptionPolicy = EKzLineAudioInterruptionPolicy::Inherit;

	/** Default audio spatialization for lines on this channel. Lines may override per-line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	EKzLineAudioSpatialization DefaultAudioSpatialization = EKzLineAudioSpatialization::Inherit;

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

/** How a dialogue advances from one line to the next. */
UENUM(BlueprintType)
enum class EKzDialogueAdvanceMode : uint8
{
	/** Resolve from the next link in the chain (callsite override > asset > Automatic). */
	Inherit,
	/** Auto-advance once the line's resolved duration elapses. */
	Automatic,
	/** Hold each line until Next() is called. */
	Manual
};

/**
 * Tuning for the "speaking level" (0..1 mouth/jaw amplitude derived from a line's audio envelope).
 * Lives in project settings as a default; a speaker can override it per character.
 */
USTRUCT(BlueprintType)
struct FKzSpeakingLevelSettings
{
	GENERATED_BODY()

	/** Multiplies the raw audio envelope before clamping to 0..1. Tune to your VO loudness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaking", meta = (ClampMin = 0.0))
	float Gain = 5.0f;

	/** Envelope at or below this counts as silence (the level falls to 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaking", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Threshold = 0.02f;

	/** Opening rate per second (CONSTANT, units of level/sec) — caps how fast the mouth opens, so the first jump from 0 ramps instead of snapping. Higher = faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaking", meta = (ClampMin = 0.0))
	float AttackSpeed = 12.0f;

	/** Closing interp speed (eased / exponential). 0 = instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaking", meta = (ClampMin = 0.0))
	float ReleaseSpeed = 12.0f;

	/** Contrast around 0.5 (sigmoid): pushes the level toward 0/1 so the mouth opens/closes crisply instead of hovering mid-open. 1 = linear, higher = more on/off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaking", meta = (ClampMin = 0.1))
	float Contrast = 2.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialogueStarted, class UKzDialoguePlayer*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueFinished, class UKzDialoguePlayer*, Player, EKzDialogueFinishReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueLineEvent, class UKzDialoguePlayer*, Player, const FKzDialogueLine&, Line);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FKzOnDialogueLineSingleEvent, class UKzDialoguePlayer*, Player, const FKzDialogueLine&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialoguePlayerEvent, class UKzDialoguePlayer*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialoguePlayerCreated, FGameplayTag, Channel, class UKzDialoguePlayer*, Player);

/** Smoothed 0..1 speaking amplitude (jaw / lip-flap). Used by the player (channel-level) and the speaker (gated per speaker). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnSpeakingLevelChanged, float, Level);