// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "KzDialogueTypes.h"
#include "KzDialogueProvider.generated.h"

class UKzDialogueAsset;

/**
 * Abstract source of dialogue lines. The player consumes lines through this contract,
 * oblivious to how they're authored: data asset, manual push, branching graph, runtime
 * generation, etc. Subclasses are EditInlineNew + Instanced so they can be embedded in
 * blueprint variables when needed.
 */
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

	/** Returns the line at the cursor without advancing. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	FKzDialogueLine Current() const;
	virtual FKzDialogueLine Current_Implementation() const PURE_VIRTUAL(UKzDialogueProvider::Current, return {};);

	/** True if the cursor is on (or before) the first line. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	bool IsFirst() const;
	virtual bool IsFirst_Implementation() const { return CursorIndex <= 0; }

	/** True if the cursor is on the last line. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	bool IsLast() const;
	virtual bool IsLast_Implementation() const { return !HasNext(); }

	/** Reset the cursor so playback can start over. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Provider")
	void Reset();
	virtual void Reset_Implementation() { CursorIndex = INDEX_NONE; }

	/** Choices presented at the current branching point. Empty for linear providers. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Provider")
	TArray<FKzDialogueChoice> GetChoices() const;
	virtual TArray<FKzDialogueChoice> GetChoices_Implementation() const { return {}; }

	/** Resolve a player-selected choice, advancing into the corresponding branch. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Provider")
	void SelectChoice(int32 ChoiceIndex);
	virtual void SelectChoice_Implementation(int32 /*ChoiceIndex*/) {}

protected:
	/** Index into the currently active line list. INDEX_NONE means "before first". */
	UPROPERTY(Transient)
	int32 CursorIndex = INDEX_NONE;
};

// ---------------------------------------------------------------------------------------
// Asset provider: linear playback of a UKzDialogueAsset.
// ---------------------------------------------------------------------------------------

UCLASS(BlueprintType, DisplayName = "Dialogue Provider (Asset)")
class KZDIALOGUE_API UKzAssetDialogueProvider : public UKzDialogueProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Provider")
	TObjectPtr<UKzDialogueAsset> Asset;

	/** Optional starting line. If invalid, starts from the beginning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Provider")
	FGuid StartLineId;

	/** Optional ending line (inclusive). If invalid, plays until the asset ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Provider")
	FGuid EndLineId;

	/** Convenience factory. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Provider", meta = (DefaultToSelf = "Outer"))
	static UKzAssetDialogueProvider* Create(UObject* Outer, UKzDialogueAsset* InAsset);

	virtual bool HasNext_Implementation() const override;
	virtual FKzDialogueLine Advance_Implementation() override;
	virtual FKzDialogueLine Current_Implementation() const override;
	virtual void Reset_Implementation() override;

private:
	int32 ResolveStartIndex() const;
	int32 ResolveEndIndex() const;
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
	virtual FKzDialogueLine Current_Implementation() const override { return Line; }
	virtual bool IsFirst_Implementation() const override { return !bConsumed; }
	virtual bool IsLast_Implementation() const override { return true; }
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
	virtual FKzDialogueLine Current_Implementation() const override;
};