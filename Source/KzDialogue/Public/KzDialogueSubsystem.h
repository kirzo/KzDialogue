// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSubsystem.generated.h"

class UKzDialoguePlayer;
class UKzDialogueProvider;
class UKzDialogueAsset;

/**
 * World-level manager of dialogue playback. Owns a UKzDialoguePlayer per channel and
 * arbitrates between requests using priority. Channels let independent dialogue streams
 * coexist (story, barks, system, tutorial...) without interfering.
 */
UCLASS()
class KZDIALOGUE_API UKzDialogueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Default channel used when callers don't pass one. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Subsystem")
	FGameplayTag DefaultChannel;

	/** Get (and lazily create) the player associated with a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* GetOrCreatePlayer(FGameplayTag InChannel);

	/** Get the player for a channel without creating one. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* FindPlayer(FGameplayTag InChannel) const;

	/**
	 * Play a provider on a given channel, respecting priority.
	 * Returns the player that is now playing it, or nullptr if
	 * rejected (lower priority than current).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority = 0, bool bStartImmediately = true);

	/** Convenience wrapper: build an asset provider and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, bool bStartImmediately = true);

	/** Convenience wrapper: build a manual single-line provider and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority = 0, bool bStartImmediately = true);

	/** Stop the dialogue on a channel (graceful, with exit animations). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void StopChannel(FGameplayTag InChannel);

	/** Stop all channels. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void StopAll();

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UKzDialoguePlayer>> Players;
};