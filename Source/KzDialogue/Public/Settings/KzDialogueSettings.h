// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSettings.generated.h"

class USoundClass;

/**
 * Project-wide settings for the KzDialogue plugin. Available under
 * Project Settings -> Plugins -> KzDialogue.
 *
 * Channel definitions declared here are consulted by UKzDialogueSubsystem to resolve
 * default priorities, enforce interruption policies, and (eventually) drive
 * cross-channel audio ducking.
 */
UCLASS(Config = KzDialogue, DefaultConfig, meta = (DisplayName = "KzDialogue"))
class KZDIALOGUE_API UKzDialogueSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UKzDialogueSettings();

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UKzDialogueSettings* Get() { return GetDefault<UKzDialogueSettings>(); }

	/** Channel used when callers don't pass one to the subsystem. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/** Fallback line duration when neither the line nor its audio defines one and ReadingSpeed is 0. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "General", meta = (ClampMin = 0.1))
	float DefaultDuration = 2.5f;

	/** Characters per second a reader takes in. A line without audio or Duration stays up max(MinReadingDuration, characters / speed). 0 uses DefaultDuration instead. */
	UPROPERTY(Config, EditAnywhere, Category = "Reading", meta = (ClampMin = 0.0))
	float ReadingSpeed = 15.0f;

	/** Per-culture reading speeds ("ja" -> 7), resolved via the culture priority chain: scripts pack different amounts per character. */
	UPROPERTY(Config, EditAnywhere, Category = "Reading", meta = (ClampMin = 0.0))
	TMap<FString, float> ReadingSpeedPerCulture;

	/** Shortest time a line sized by reading speed stays up. */
	UPROPERTY(Config, EditAnywhere, Category = "Reading", meta = (ClampMin = 0.1, Units = "Seconds"))
	float MinReadingDuration = 1.0f;

	/** Active-culture reading speed: the per-culture entry matching the priority chain, ReadingSpeed otherwise. */
	float GetActiveReadingSpeed() const;

	/** The player's reading speed option, process-wide and not saved: the game applies it from its own options. 0.5 reads at half speed, so lines without audio stay twice as long. */
	static void SetReadingSpeedScale(float Scale);
	static float GetReadingSpeedScale();

	/** Seconds a dialogue waits for views that claimed a phase before advancing anyway (a removed widget, a missed notify). 0 waits forever. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (ClampMin = 0.0, Units = "Seconds"))
	float ViewResponseTimeout = 10.0f;

	/**
	 * Default policy for what happens to a line's audio when the player transitions to the next line.
	 * Lines and channels may override. An Inherit smuggled in via a hand-edited ini resolves as Stop.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (ValidEnumValues = "Stop, ContinueIfDifferentSpeaker, Continue"))
	EKzLineAudioInterruptionPolicy DefaultAudioInterruptionPolicy = EKzLineAudioInterruptionPolicy::ContinueIfDifferentSpeaker;

	/** Default placement for line audio when neither the line nor its channel override it. An Inherit smuggled in via a hand-edited ini resolves as AttachedToSpeaker. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (ValidEnumValues = "TwoD, AttachedToSpeaker"))
	EKzLineAudioSpatialization DefaultAudioSpatialization = EKzLineAudioSpatialization::AttachedToSpeaker;

	/** What a line WITHOUT audio means by default: pending recording work, or text-only by design (a game with no VO sets TextOnly once here). Lines may override. An Inherit smuggled in via a hand-edited ini resolves as VoiceExpected. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (ValidEnumValues = "VoiceExpected, TextOnly"))
	EKzLineVoicePolicy DefaultVoicePolicy = EKzLineVoicePolicy::VoiceExpected;

	/** Localization Dashboard target that dialogue translation CSVs are imported into (usually "Game"). Editor-only workflow, harmless at runtime. */
	UPROPERTY(Config, EditAnywhere, Category = "Localization")
	FString LocalizationTargetName = TEXT("Game");

	/** Culture holding the machine-generated pseudo-localization (lengthened, accented, bracketed texts for overflow/font/hardcoded-text QA). Added to the target automatically on generate. Keep it OUT of the packaging CulturesToStage: QA builds opt in with -CookCultures. Empty disables the tool. */
	UPROPERTY(Config, EditAnywhere, Category = "Localization")
	FString PseudoCulture = TEXT("qps-ploc");

	/** FText::Format named-arg pattern composing structured speaker names. Available args: {Qualifier}, {Honorific}, {Given}, {Family}, {Family2}, {Nick}. Default for cultures without an override. Plain FString on purpose: it is per-culture by construction and must not enter the localization gather. */
	UPROPERTY(Config, EditAnywhere, Category = "Speakers")
	FString DefaultSpeakerNameFormat = TEXT("{Qualifier} {Honorific} {Given} {Family} {Family2}");

	/** Per-culture format overrides ("ja" -> "{Family} {Given}{Honorific}"), resolved via the culture priority chain (es-MX falls back to es before the default). */
	UPROPERTY(Config, EditAnywhere, Category = "Speakers")
	TMap<FString, FString> SpeakerNameFormatsPerCulture;

	/** Active-culture name format: the per-culture override matching the priority chain, DefaultSpeakerNameFormat otherwise. */
	const FString& GetActiveSpeakerNameFormat() const;

	/** Default mouth/jaw "speaking level" tuning derived from a line's audio. Speakers can override per character. */
	UPROPERTY(Config, EditAnywhere, Category = "Speaking")
	FKzSpeakingLevelSettings SpeakingDefaults;

	/**
	 * Per-channel configuration. Channels not listed here are still accepted at runtime
	 * with default values, but a warning is logged so the project author can decide
	 * whether to formalize the channel.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Channels")
	TArray<FKzDialogueChannelDefinition> Channels;

	/**
	 * Routes lines to a channel through their audio's SoundClass. Consulted by the channel
	 * resolution chain between the line's own DefaultChannel and the asset's: a line whose
	 * audio uses a mapped SoundClass plays on the mapped channel unless something more
	 * specific says otherwise. Matches hierarchically: an unmapped SoundClass inherits the
	 * nearest mapped ancestor's channel, and an exact entry overrides the hierarchy.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Channels", meta = (Categories = "Dialogue.Channel"))
	TMap<TSoftObjectPtr<USoundClass>, FGameplayTag> SoundClassChannels;

	/** The definition declared for Tag or its nearest declared ancestor, so every Dialogue.Channel.Bark.* channel uses Dialogue.Channel.Bark's. Null when none is declared. */
	const FKzDialogueChannelDefinition* FindChannel(const FGameplayTag& Tag) const;

	/** Channel mapped to a SoundClass or its nearest mapped ancestor; empty when the whole parent chain is unmapped (or maps to invalid channels). */
	FGameplayTag FindChannelForSoundClass(const USoundClass* SoundClass) const;
};