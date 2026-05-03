// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSubsystem.h"
#include "KzDialogueFunctionLibrary.generated.h"

class UKzDialogueAsset;
class UKzDialoguePlayer;

UCLASS()
class KZDIALOGUE_API UKzDialogueFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Retrieves the dialogue player for a specific channel.
	 * @param bCreateIfNotFound If true, creates a new player if one is not currently active for the channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static UKzDialoguePlayer* GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound = true);

	/** One-shot helper: play an asset on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGameplayTag Channel, bool bStartImmediately = false);

	/** One-shot helper: play a single line on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLine& Line, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = false);

	/** Stop the dialogue on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject"))
	static void StopDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel);
};