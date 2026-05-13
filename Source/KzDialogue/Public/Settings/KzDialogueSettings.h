// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSettings.generated.h"

/**
 * Project-wide settings for the KzDialogue plugin. Available under
 * Project Settings -> Plugins -> KzDialogue.
 *
 * Channel definitions declared here are consulted by UKzDialogueSubsystem to clamp
 * priorities, enforce interruption policies, and (eventually) drive cross-channel
 * audio ducking.
 */
UCLASS(Config = KzDialogue, DefaultConfig, meta = (DisplayName = "KzDialogue"))
class KZDIALOGUE_API UKzDialogueSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UKzDialogueSettings();

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Convenience accessor. */
	static const UKzDialogueSettings* Get() { return GetDefault<UKzDialogueSettings>(); }

	/** Channel used when callers don't pass one to the subsystem. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/** Fallback line duration when neither the line nor its audio defines one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (ClampMin = 0.1))
	float DefaultDuration = 2.5f;

	/**
	 * Default policy for what happens to a line's audio when the player transitions to the next line.
	 * Lines and channels may override.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	EKzLineAudioInterruptionPolicy DefaultAudioInterruptionPolicy = EKzLineAudioInterruptionPolicy::ContinueIfDifferentSpeaker;

	/**
	 * Per-channel configuration. Channels not listed here are still accepted at runtime
	 * with default values, but a warning is logged so the project author can decide
	 * whether to formalize the channel.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Channels")
	TArray<FKzDialogueChannelDefinition> Channels;

	/** Lookup helper. Returns nullptr if the channel is not defined. */
	const FKzDialogueChannelDefinition* FindChannel(const FGameplayTag& Tag) const;
};