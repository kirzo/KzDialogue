// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsyncActions.generated.h"

class UKzDialogueAsset;
class UKzDialoguePlayer;
class UKzDialogueSubsystem;
class UKzDialogueAssetSession;

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

	/** Stops the dialogue gracefully (exit animation) and resolves the action as Cancelled. No-op once finished. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	virtual void Stop();

	/**
	 * Interrupts the dialogue immediately and resolves the action as Cancelled. After the action
	 * resolved it still works as "silence what I launched": it interrupts this action's own line
	 * while it winds down (exit fades / audio outliving the line) and stops Continue-policy tails
	 * ringing on the idle channel — but never touches content that took the channel afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	virtual void Interrupt();

protected:
	/** Resolves (and creates) the channel player. False when no player is available. */
	bool AcquirePlayer();

	/**
	 * Resolves the definitive channel this action will play on, applying the same chain the
	 * subsystem's play path applies (explicit > line/entry > asset > settings). Keeps the
	 * pre-acquired player identical to the one playback lands on.
	 */
	virtual FGameplayTag ResolveLaunchChannel(const UKzDialogueSubsystem& Subsystem) const { return Channel; }

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

	/**
	 * True when Line is content this action launched (the awaited ref / entries). Drives the
	 * post-resolve Interrupt's ours-only check: the specific-line Finished fires when the line's
	 * EXIT starts, so the action can be resolved while its own dialogue (fades, audio) still runs.
	 */
	virtual bool IsOwnContent(UKzDialoguePlayer& Player, const FKzDialogueLine& Line) const { return false; }

	UPROPERTY(Transient)
	TObjectPtr<UObject> WorldContext;

	UPROPERTY(Transient)
	TObjectPtr<UKzDialoguePlayer> DialoguePlayer;

	/** Weak shadow of DialoguePlayer surviving SetReadyToDestroy, so a late Interrupt can still silence tails. */
	TWeakObjectPtr<UKzDialoguePlayer> WeakDialoguePlayer;

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
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line Ref (Async)", AutoCreateRefTerm = "Line", AdvancedDisplay = "Channel,Priority,bStartImmediately"))
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
	virtual FGameplayTag ResolveLaunchChannel(const UKzDialogueSubsystem& Subsystem) const override;
	virtual void HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line) override;
	virtual void NotifyCancelled() override;
	virtual bool IsOwnContent(UKzDialoguePlayer& Player, const FKzDialogueLine& Line) const override;

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
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line From Asset (Async)", AdvancedDisplay = "Channel,Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLineInline* PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);
};

/**
 * Shared logic for the multi-entry async actions: plays a sequence as one dialogue and completes
 * when the LAST entry finishes. LineFinished fires once per completed entry. Entries are awaited
 * one at a time in playback order, so repeated ids and aliases resolve correctly. Cancelled fires
 * when the dialogue ends before the sequence completed.
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzAsyncDialogueSequenceAction : public UKzAsyncDialogueAction
{
	GENERATED_BODY()

public:
	/** Fired right after the sequence is requested. Carries the channel player; Line is not set yet. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Started;

	/** Fired every time one entry of the sequence finishes playing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent LineFinished;

	/** Fired when the last entry of the sequence finishes playing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Finished;

	/** Fired when the dialogue ends before the sequence completed, or nothing could play. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Cancelled;

	virtual void Activate() override;

protected:
	virtual FGameplayTag ResolveLaunchChannel(const UKzDialogueSubsystem& Subsystem) const override;

	/** Fills OutRefs with the awaited entries, in playback order. */
	virtual void GatherEntryRefs(TArray<FKzDialogueLineRef>& OutRefs) const {}

	/** Requests playback of the whole sequence. Returns the playing channel player on success. */
	virtual UKzDialoguePlayer* LaunchPlayback() { return nullptr; }

	virtual void HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line) override;
	virtual void NotifyCancelled() override;
	virtual bool IsOwnContent(UKzDialoguePlayer& Player, const FKzDialogueLine& Line) const override;

	/** Binds the next awaitable entry starting at CurrentIndex, skipping unresolvable ones. False when none is left. */
	bool BindNextEntry();

	/** Per-entry refs, in playback order. */
	TArray<FKzDialogueLineRef> EntryRefs;

	/** Entry currently awaited. */
	int32 CurrentIndex = 0;
};

/** Plays a FKzDialogueLineList (one asset, several ids) as one sequential dialogue. */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueLineList : public UKzAsyncDialogueSequenceAction
{
	GENERATED_BODY()

public:
	/** Plays a list of dialogue lines sequentially and waits until the last one finishes. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line List (Async)", AutoCreateRefTerm = "Lines", AdvancedDisplay = "Channel,Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLineList* PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& Lines, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

protected:
	virtual void GatherEntryRefs(TArray<FKzDialogueLineRef>& OutRefs) const override;
	virtual UKzDialoguePlayer* LaunchPlayback() override;

	/** List awaited by this action. */
	FKzDialogueLineList LaunchList;
};

/** Plays an array of FKzDialogueLineRef (possibly spanning assets) as one sequential dialogue. */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueLineRefs : public UKzAsyncDialogueSequenceAction
{
	GENERATED_BODY()

public:
	/** Plays an array of dialogue line refs sequentially and waits until the last one finishes. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Line Refs (Async)", AutoCreateRefTerm = "Lines", AdvancedDisplay = "Channel,Priority,bStartImmediately"))
	static UKzAsyncPlayDialogueLineRefs* PlayDialogueLineRefs(const UObject* WorldContextObject, const TArray<FKzDialogueLineRef>& Lines, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, int32 Priority = -1, bool bStartImmediately = true);

protected:
	virtual void GatherEntryRefs(TArray<FKzDialogueLineRef>& OutRefs) const override;
	virtual UKzDialoguePlayer* LaunchPlayback() override;

	/** Refs awaited by this action. */
	TArray<FKzDialogueLineRef> LaunchRefs;
};

/**
 * Plays a whole dialogue asset (advancing through all of its lines) and completes when the dialogue
 * ends. Finished fires on natural completion; Cancelled fires when it is stopped, aborted or
 * interrupted before reaching the end.
 */
UCLASS(meta = (HasDedicatedAsyncNode))
class KZDIALOGUE_API UKzAsyncPlayDialogueAsset : public UKzAsyncDialogueAction
{
	GENERATED_BODY()

public:
	/** Plays a dialogue asset and waits until the whole dialogue finishes. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Asset (Async)", AdvancedDisplay = "Channel,bStartImmediately,AdvanceMode"))
	static UKzAsyncPlayDialogueAsset* PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, UPARAM(meta = (Categories = "Dialogue.Channel")) FGameplayTag Channel, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

	/** Fired right after the asset is requested. Carries the channel player; Line is not set yet. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Started;

	/** Fired when the dialogue reaches its end (Completed). */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Finished;

	/** Fired when the dialogue is stopped, aborted or interrupted before completing. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueLineEvent Cancelled;

	virtual void Activate() override;
	virtual void Stop() override;
	virtual void Interrupt() override;
	virtual void SetReadyToDestroy() override;

protected:
	virtual void NotifyCancelled() override;

	UFUNCTION()
	void HandleAssetFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	/** Asset awaited by this action. */
	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueAsset> Asset;

	/** Session driving the asset across its (possibly several) channels; the whole-asset finish routes through it. */
	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueAssetSession> Session;

	/** Advance mode passed to PlayAsset (Inherit = the asset's own setting). */
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit;
};