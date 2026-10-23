// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzDialogueTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KzDialogueMemorySubsystem.generated.h"

/** What an alias's selection mode remembers between picks (FKzDialogueAlias::SelectionMode). */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzAliasMemory
{
	GENERATED_BODY()

	/** Last line picked; RandomNoRepeat avoids it and a new shuffle bag never starts with it. Invalid = nothing picked yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FGuid LastPickedLineId;

	/** ShuffleBag: the current bag in shuffle order, drained as lines play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TArray<FGuid> ShuffleBag;

	/** ShuffleBag: index of the next line to take from the bag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 ShuffleCursor = 0;

	/** ShuffleBag: the alias's lines when the bag was built; a change rebuilds it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TArray<FGuid> ShuffleBagSourceIds;

	/** Sequential: index of the next line to play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 SequentialCursor = 0;
};

/** Serializable snapshot of the dialogue memory: store it in the game's save and re-apply on load. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueMemory
{
	GENERATED_BODY()

	/** Times each asset line was presented, by LineId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TMap<FGuid, int32> SeenLines;

	/** Pick state of each alias, by AliasId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TMap<FGuid, FKzAliasMemory> Aliases;
};

/**
 * Game-instance memory of dialogue, surviving level travel: which asset lines were presented and how
 * many times, and what each alias remembers between picks (shuffle bags, the last pick, sequential
 * cursors). Alias cooldowns are world time and stay in UKzDialogueSubsystem. Persistence is the game's
 * business through the snapshot: GetDialogueMemory into the save, ApplyDialogueMemory on load.
 */
UCLASS()
class KZDIALOGUE_API UKzDialogueMemorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** True when the line, or any line of the alias, has been presented. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Memory")
	bool HasSeenLine(const FKzDialogueLineRef& Ref) const;

	/** Times the line was presented; for an alias, the sum over its lines. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Memory")
	int32 GetLineSeenCount(const FKzDialogueLineRef& Ref) const;

	/** Forgets the line; for an alias, every line of it and its pick state. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory")
	void ForgetLine(const FKzDialogueLineRef& Ref);

	/** Forgets every presented line and every alias pick state. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory")
	void ForgetAllLines() { Memory = FKzDialogueMemory(); }

	/** Snapshot for the game's save data. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Memory")
	FKzDialogueMemory GetDialogueMemory() const { return Memory; }

	/** Restores a saved snapshot, replacing the current memory. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory")
	void ApplyDialogueMemory(const FKzDialogueMemory& InMemory) { Memory = InMemory; }

	/** Counts a presented line. Lines not resolved from an asset have no stable id and are ignored. Called by the players. */
	void RecordLinePresented(const FKzDialogueLine& Line);

	/** The pick state of an alias, created on first use. */
	FKzAliasMemory& FindOrAddAliasMemory(const FGuid& AliasId) { return Memory.Aliases.FindOrAdd(AliasId); }

	void ForgetAlias(const FGuid& AliasId) { Memory.Aliases.Remove(AliasId); }
	void ForgetAllAliases() { Memory.Aliases.Reset(); }

	/** The memory reachable through WorldContextObject's game instance, or null (editor previews have none). */
	static UKzDialogueMemorySubsystem* Get(const UObject* WorldContextObject);

private:
	UPROPERTY(Transient)
	FKzDialogueMemory Memory;
};