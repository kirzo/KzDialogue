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
class UKzDialogueAssetSession;

UCLASS()
class KZDIALOGUE_API UKzDialogueFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns true if the speaker is meaningfully set (references a speaker asset). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker", meta = (DisplayName = "Is Valid", CompactNodeTitle = "IsValid"))
	static bool IsDialogueSpeakerValid(const FKzDialogueSpeaker& Speaker);

	/**
	 * Retrieves the dialogue player for a specific channel.
	 * @param bCreateIfNotFound If true, creates a new player if one is not currently active for the channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static UKzDialoguePlayer* GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound = true);

	/**
	 * One-shot helper: play a whole asset on a channel. Returns a session that completes once for the whole
	 * asset (each line resolves its own channel; runs chain across channel changes). AdvanceMode defaults to
	 * the asset's (Automatic / Manual RPG-style).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately,AdvanceMode"))
	static UKzDialogueAssetSession* PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGameplayTag Channel, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

	/** Resolve a single line by GUID inside a dialogue asset and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play a dialogue line referenced by a FKzDialogueLineRef. The runtime resolves
	 * the reference (loading the asset if needed) and dispatches to the dialogue
	 * subsystem on the given channel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Ref, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * One-shot helper: play a single line on a channel, exactly as given. The line's notify Timeline
	 * is only present if Line was resolved from an asset (via PlayDialogueLineFromAsset, a
	 * FKzDialogueLineRef, or TryResolveDialogueLineRef before this call); a line built by hand carries
	 * no timeline, since timelines live on the asset keyed by LineId.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineDirect(const UObject* WorldContextObject, const FKzDialogueLine& Line, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Resolve a FKzDialogueLineRef to its concrete line without playing it. Useful
	 * for previewing or driving custom UI without invoking the dialogue subsystem.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dialogue")
	static bool TryResolveDialogueLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine);

	/**
	 * Play a list of dialogue lines (or aliases) as ONE sequential dialogue. Each entry plays on its
	 * own resolved channel (an explicit Channel forces the whole list onto it), chaining consecutive
	 * same-channel entries as runs, like PlayDialogueAsset. Aliases are resolved at launch; entries
	 * that fail to resolve are skipped. Returns a session whose OnDialogueFinished fires once when
	 * the whole list ends.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately"))
	static UKzDialogueAssetSession* PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& List, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play an array of line refs (possibly spanning multiple assets) as ONE sequential
	 * dialogue on the given channel. Aliases are resolved at launch; entries that fail to
	 * resolve are skipped. Line events fire per entry and OnDialogueFinished once at the end.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel", AdvancedDisplay = "Channel,bStartImmediately"))
	static UKzDialoguePlayer* PlayDialogueLineRefs(const UObject* WorldContextObject, const TArray<FKzDialogueLineRef>& Refs, FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

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
	 * Effective tags of a resolved line: its own Tags, which already include the asset-wide tags merged in
	 * at resolve time. A view only ever gets the resolved line, so this (and the LineHas* helpers below)
	 * are all it needs — no asset reference required.
	 */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Line")
	static FGameplayTagContainer GetLineTags(const FKzDialogueLine& Line);

	/** True if the line has Tag. bExact uses HasTagExact (no parent matching). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Line")
	static bool LineHasTag(const FKzDialogueLine& Line, FGameplayTag Tag, bool bExact = false);

	/** True if the line has ANY of Tags. bExact uses HasAnyExact. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Line", meta = (AutoCreateRefTerm = "Tags"))
	static bool LineHasAnyTags(const FKzDialogueLine& Line, const FGameplayTagContainer& Tags, bool bExact = false);

	/** True if the line has ALL of Tags. bExact uses HasAllExact. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Line", meta = (AutoCreateRefTerm = "Tags"))
	static bool LineHasAllTags(const FKzDialogueLine& Line, const FGameplayTagContainer& Tags, bool bExact = false);

	/** Set a text argument for the line's "{Name}" placeholder. Call on your line copy before playing it; views format at display time. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Line")
	static void SetLineTextArgument(UPARAM(ref) FKzDialogueLine& Line, FName Name, FText Value);

	/** Set a numeric argument for the line's "{Name}" placeholder. Numbers drive the pattern's |plural(...) forms per culture. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Line")
	static void SetLineNumberArgument(UPARAM(ref) FKzDialogueLine& Line, FName Name, double Value);

	/** Line text with its format arguments applied (Text as-is when it has none). What the subtitle views render. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Line")
	static FText GetLineFormattedText(const FKzDialogueLine& Line);

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

	/** Stop all dialogues. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject"))
	static void StopAllDialogues(const UObject* WorldContextObject);

	/** Interrupts the dialogue on a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", Categories = "Dialogue.Channel"))
	static void InterruptDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel);

	/** Interrupt all dialogues. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject"))
	static void InterruptAllDialogues(const UObject* WorldContextObject);
};