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
	/** Returns true if the speaker is meaningfully set (has either an override name or a valid tag). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker", meta = (DisplayName = "Is Valid", CompactNodeTitle = "IsValid"))
	static bool IsDialogueSpeakerValid(const FKzDialogueSpeaker& Speaker);

	/**
	 * Retrieves the dialogue player for a specific channel.
	 * @param bCreateIfNotFound If true, creates a new player if one is not currently active for the channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static UKzDialoguePlayer* GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound = true);

	/** One-shot helper: play an asset on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGameplayTag Channel, bool bStartImmediately = true);

	/** Resolve a single line by GUID inside a dialogue asset and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play a dialogue line referenced by a FKzDialogueLineRef. The runtime resolves
	 * the reference (loading the asset if needed) and dispatches to the dialogue
	 * subsystem on the given channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Ref, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/** One-shot helper: play a single line on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineDirect(const UObject* WorldContextObject, const FKzDialogueLine& Line, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Resolve a FKzDialogueLineRef to its concrete line without playing it. Useful
	 * for previewing or driving custom UI without invoking the dialogue subsystem.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dialogue")
	static bool TryResolveDialogueLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine);

	/**
	 * Play a list of dialogue lines (or aliases) sequentially on the given channel. The
	 * first one starts immediately if bStartImmediately is true; subsequent entries are
	 * queued behind it on the same channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& List, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Resolve a FKzDialogueLineList into its concrete lines without playing them. The
	 * output preserves order; entries that fail to resolve are skipped silently.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dialogue")
	static bool TryResolveDialogueLineList(const FKzDialogueLineList& List, TArray<FKzDialogueLine>& OutLines);

	/**
	 * Convert a FKzDialogueLineList into a list of FKzDialogueLineRef. Each output ref
	 * points at the same asset and one of the list's GUIDs. Useful as an adapter when
	 * downstream code consumes refs (one-by-one playback, custom UI, gameplay logic
	 * that picks one entry at random, etc.).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dialogue")
	static void GetDialogueLineRefsFromList(const FKzDialogueLineList& List, TArray<FKzDialogueLineRef>& OutRefs);

	/**
	 * Returns true when a dialogue player exists for the given channel and is currently
	 * playing a line. Useful to gate input, branch logic ("don't trigger this barks if
	 * something is talking on Main"), or trigger animations.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static bool IsDialogueChannelPlaying(const UObject* WorldContextObject, FGameplayTag Channel);

	/** Stop the dialogue on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static void StopDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel);
};