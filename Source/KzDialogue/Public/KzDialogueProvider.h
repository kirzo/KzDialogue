// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "KzDialogueTypes.h"
#include "KzDialogueProvider.generated.h"

/** Abstract source of the lines a player consumes, whatever their origin (manual push, a resolved list, runtime generation). */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class KZDIALOGUE_API UKzDialogueProvider : public UObject
{
	GENERATED_BODY()

public:
	/** Whether there is at least one more line to play after the current one. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	bool HasNext() const;
	virtual bool HasNext_Implementation() const PURE_VIRTUAL(UKzDialogueProvider::HasNext, return false;);

	/** Advance the cursor and return the new current line. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Provider")
	FKzDialogueLine Advance();
	virtual FKzDialogueLine Advance_Implementation() PURE_VIRTUAL(UKzDialogueProvider::Advance, return {};);

	/** The line Advance would return, without advancing; the player prefetches its audio. Empty when not known yet. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	FKzDialogueLine PeekNext() const;
	virtual FKzDialogueLine PeekNext_Implementation() const { return {}; }

	/** Reset the cursor so playback can start over. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Provider")
	void Reset();
	virtual void Reset_Implementation() {}
};

// ---------------------------------------------------------------------------------------
// Manual provider: a single ad-hoc line, queued/replaced from gameplay code.
// Useful for system messages, barks, or anything that doesn't deserve a full asset.
// ---------------------------------------------------------------------------------------

UCLASS(BlueprintType, DisplayName = "Dialogue Provider (Manual)")
class KZDIALOGUE_API UKzManualDialogueProvider : public UKzDialogueProvider
{
	GENERATED_BODY()

public:
	/** The pending line. May be replaced while playing to chain manual lines. */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Provider")
	FKzDialogueLine Line;

	/** True after the cursor has consumed Line at least once. */
	UPROPERTY(Transient)
	bool bConsumed = false;

	/** Convenience factory. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Provider", meta = (DefaultToSelf = "Outer"))
	static UKzManualDialogueProvider* Create(UObject* Outer, const FKzDialogueLine& InLine);

	/** Replace the queued line. Marks it as not-yet-consumed so the player will play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Provider")
	void SetLine(const FKzDialogueLine& InLine);

	virtual bool HasNext_Implementation() const override { return !bConsumed; }
	virtual FKzDialogueLine Advance_Implementation() override;
	virtual void Reset_Implementation() override { bConsumed = false; }
};

// ---------------------------------------------------------------------------------------
// Line list provider: linear playback of pre-resolved lines as ONE dialogue session.
// Backs PlayDialogueLineList: line events fire per entry and OnDialogueFinished once at
// the end, instead of one dialogue per entry.
// ---------------------------------------------------------------------------------------

UCLASS(BlueprintType, DisplayName = "Dialogue Provider (Line List)")
class KZDIALOGUE_API UKzLineListDialogueProvider : public UKzDialogueProvider
{
	GENERATED_BODY()

public:
	/** Resolved lines, in playback order. */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue|Provider")
	TArray<FKzDialogueLine> Lines;

	/** Convenience factory. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Provider", meta = (DefaultToSelf = "Outer"))
	static UKzLineListDialogueProvider* Create(UObject* Outer, const TArray<FKzDialogueLine>& InLines);

	virtual bool HasNext_Implementation() const override { return CursorIndex + 1 < Lines.Num(); }
	virtual FKzDialogueLine Advance_Implementation() override;
	virtual FKzDialogueLine PeekNext_Implementation() const override { return Lines.IsValidIndex(CursorIndex + 1) ? Lines[CursorIndex + 1] : FKzDialogueLine{}; }
	virtual void Reset_Implementation() override { CursorIndex = INDEX_NONE; }

private:
	/** Index of the line being played; INDEX_NONE before the first. */
	UPROPERTY(Transient)
	int32 CursorIndex = INDEX_NONE;
};