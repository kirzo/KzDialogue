// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsyncActions.generated.h"

class UKzDialogueAsset;
class UKzDialoguePlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzAsyncDialogueLineEvent, UKzDialoguePlayer*, Player, const FKzDialogueLine&, Line);

/**
 * Shared plumbing for the dialogue async actions: channel player acquisition, specific-line
 * binding bookkeeping, cancellation detection and teardown.
 * Order matters twice here: the specific-line binding is created BEFORE playback is requested
 * (a line can never finish in the gap between playing and subscribing), while the
 * dialogue-finished watchdog is subscribed AFTER (launching may interrupt previous content on
 * the channel, which broadcasts OnDialogueFinished during the play call).
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzAsyncDialogueAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	virtual void SetReadyToDestroy() override;

protected:
	/** Resolves (and creates) the channel player. False when no player is available. */
	bool AcquirePlayer();

	/** Binds HandleSpecificLineFinished to Ref on the acquired player. False when the ref cannot be resolved. */
	bool BindSpecificLineFinished(const FKzDialogueLineRef& Ref);

	/** Subscribes the dialogue-finished watchdog. Call only after playback has been requested. */
	void SubscribeDialogueFinished();

	/** Removes the specific-line binding and the dialogue-finished watchdog. */
	void CleanupBindings();

	UFUNCTION()
	virtual void HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line) {}

	UFUNCTION()
	void HandleDialogueFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	/** The dialogue ended before the awaited line(s) completed. Leaf actions broadcast Cancelled and tear down. */
	virtual void NotifyCancelled() {}

	UPROPERTY(Transient)
	TObjectPtr<UObject> WorldContext;

	UPROPERTY(Transient)
	TObjectPtr<UKzDialoguePlayer> DialoguePlayer;

	FGameplayTag Channel;
	int32 Priority = -1;
	bool bStartImmediately = true;
	FGuid LineBindingHandle;
};

/**
 * Plays a single dialogue line and completes when THAT line finishes, even if other content is
 * playing on the channel. Cancelled fires when the dialogue ends before the line completed, or
 * when the line cannot be played at all.
 */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueLine : public UKzAsyncDialogueAction
{
	GENERATED_BODY()

public:
	/** Plays a dialogue line and waits for that specific line to finish. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line Ref (Async)", AutoCreateRefTerm = "Line", AdvancedDisplay = "Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLine* PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Line, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/** Fired right after the line is requested. Carries the channel player; Line is not set yet. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Started;

	/** Fired when the requested line finishes playing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Finished;

	/** Fired when the dialogue ends before the requested line completed, or the line could not play. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Cancelled;

	virtual void Activate() override;

protected:
	virtual void HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line) override;
	virtual void NotifyCancelled() override;

	/** Line awaited by this action. */
	FKzDialogueLineRef LaunchRef;
};

/**
 * Variant of UKzAsyncPlayDialogueLine taking the raw Asset + LineId pair, exposed by
 * UK2Node_PlayDialogueLineAsync with the inline line dropdown.
 */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueLineInline : public UKzAsyncPlayDialogueLine
{
	GENERATED_BODY()

public:
	/** Plays a line from a dialogue asset and waits for that specific line to finish. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line From Asset (Async)", AdvancedDisplay = "Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLineInline* PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);
};

/**
 * Plays a list of dialogue lines as one sequential dialogue and completes when the LAST entry
 * finishes. LineFinished fires once per completed entry. Entries are awaited one at a time in
 * list order, so repeated ids and aliases resolve correctly. Cancelled fires when the dialogue
 * ends before the list completed.
 */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueLineList : public UKzAsyncDialogueAction
{
	GENERATED_BODY()

public:
	/** Plays a list of dialogue lines sequentially and waits until the last one finishes. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line List (Async)", AutoCreateRefTerm = "Lines", AdvancedDisplay = "Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLineList* PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& Lines, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

	/** Fired right after the list is requested. Carries the channel player; Line is not set yet. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Started;

	/** Fired every time one entry of the list finishes playing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent LineFinished;

	/** Fired when the last entry of the list finishes playing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Finished;

	/** Fired when the dialogue ends before the list completed, or nothing could play. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Cancelled;

	virtual void Activate() override;

protected:
	virtual void HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line) override;
	virtual void NotifyCancelled() override;

	/** Binds the next awaitable entry starting at CurrentIndex, skipping unresolvable ones. False when none is left. */
	bool BindNextEntry();

	/** List awaited by this action. */
	FKzDialogueLineList LaunchList;

	/** Per-entry refs, in playback order. */
	TArray<FKzDialogueLineRef> EntryRefs;

	/** Entry currently awaited. */
	int32 CurrentIndex = 0;
};