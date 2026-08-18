// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAssetSession.generated.h"

class UKzDialogueSubsystem;
class UKzDialoguePlayer;
class UKzDialogueAsset;

/**
 * Plays a whole dialogue asset whose consecutive lines may resolve to different channels. It splits the
 * asset's lines into maximal runs that share a resolved channel and plays each run as one session
 * (UKzDialogueSubsystem::PlayAssetLineList) on its channel, chaining the next run when the current one
 * completes naturally. A single-channel asset collapses to one run, identical to the old single-session
 * play.
 *
 * Mirrors a player's wait surface so callers wait on the WHOLE asset, not one channel: OnDialogueFinished
 * fires exactly ONCE for the asset, plus IsPlaying() / Stop() / Interrupt(). Owned by the subsystem
 * (kept alive while it runs) and released when it finishes.
 *
 * Note: the subtitle box "stays up" only WITHIN a run; a channel change between runs is a clean hand-off
 * (the old channel's box exits, the new channel's enters), which is the point of per-line channels.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialogueAssetSession : public UObject
{
	GENERATED_BODY()

public:
	/** Fired exactly once when the whole asset ends. Reason: Completed (natural end or graceful Stop), Interrupted, Aborted. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Asset Session")
	FKzOnDialogueFinished OnDialogueFinished;

	/** True while a run is active (between the first launch and the final finish). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset Session")
	bool IsPlaying() const { return bActive; }

	/** The player of the current run, or null. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset Session")
	UKzDialoguePlayer* GetCurrentPlayer() const { return CurrentPlayer; }

	/** The channel the current run plays on. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset Session")
	FGameplayTag GetChannel() const { return CurrentChannel; }

	/** The finish reason; meaningful once IsPlaying() is false (Completed by default). */
	EKzDialogueFinishReason GetFinishReason() const { return FinishReason; }

	/** Graceful stop of the whole asset (exit animation on the current run). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Asset Session")
	void Stop();

	/** Hard stop of the whole asset. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Asset Session")
	void Interrupt();

	/** Subsystem entry point: split the asset into runs and start. Call UKzDialogueSubsystem::PlayAsset, not this. */
	void Start(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, FGameplayTag ExplicitChannel, bool bInStartImmediately, EKzDialogueAdvanceMode InAdvanceMode);

	/** Subsystem entry point: same as Start, but over an explicit ordered subset of the asset's entries (lines or aliases). Call UKzDialogueSubsystem::PlayLineList, not this. */
	void StartEntries(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, const TArray<FGuid>& EntryIds, FGameplayTag ExplicitChannel, int32 InPriority, bool bInStartImmediately, EKzDialogueAdvanceMode InAdvanceMode);

private:
	void LaunchRun(int32 RunIndex);

	UFUNCTION()
	void HandleRunFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	/** Broadcasts OnDialogueFinished once and releases from the subsystem. Idempotent. */
	void FinishSession(EKzDialogueFinishReason Reason);

	/** A maximal run of consecutive lines sharing a resolved channel. */
	struct FRun
	{
		FGameplayTag Channel;
		TArray<FGuid> LineIds;
	};

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueSubsystem> Subsystem;

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueAsset> Asset;

	UPROPERTY(Transient)
	TObjectPtr<UKzDialoguePlayer> CurrentPlayer;

	TArray<FRun> Runs;
	int32 CurrentRunIndex = INDEX_NONE;
	FGameplayTag CurrentChannel;

	/** Caller's priority request, applied to every run; -1 (InheritPriority) falls back to the asset hint / channel default. */
	int32 Priority = -1;
	EKzDialogueAdvanceMode ResolvedAdvanceMode = EKzDialogueAdvanceMode::Automatic;
	EKzDialogueFinishReason FinishReason = EKzDialogueFinishReason::Completed;
	bool bStartImmediately = true;
	bool bActive = false;
	bool bFinished = false;
	bool bTearingDown = false;
};
