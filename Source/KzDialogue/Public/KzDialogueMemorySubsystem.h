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

/** What a dialogue graph's plays went through, by node or option id. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueGraphVisits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TMap<FGuid, int32> Visits;
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

	/** Facts noted by dialogue graphs or gameplay, with how many times each was noted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TMap<FGameplayTag, int32> Facts;

	/** The nodes and options each dialogue graph's plays went through, by graph id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TMap<FGuid, FKzDialogueGraphVisits> Graphs;
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
	void ForgetAllLines() { Memory.SeenLines.Reset(); Memory.Aliases.Reset(); }

	/** Notes a fact, adding Count to how many times it was noted. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory|Facts")
	void AddFact(FGameplayTag Fact, int32 Count = 1);

	/** Sets how many times a fact was noted; zero forgets it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory|Facts")
	void SetFactCount(FGameplayTag Fact, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Memory|Facts")
	void RemoveFact(FGameplayTag Fact) { Memory.Facts.Remove(Fact); }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Memory|Facts")
	int32 GetFactCount(FGameplayTag Fact) const { const int32* Count = Memory.Facts.Find(Fact); return Count ? *Count : 0; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Memory|Facts")
	bool HasFact(FGameplayTag Fact) const { return GetFactCount(Fact) > 0; }

	/** Counts a play going through a node or choosing an option of a dialogue graph. Called by the graph instances. */
	void RecordGraphVisit(const FGuid& GraphId, const FGuid& Id) { ++Memory.Graphs.FindOrAdd(GraphId).Visits.FindOrAdd(Id); }

	/** Times the plays of a dialogue graph went through a node or chose an option. */
	int32 GetGraphVisitCount(const FGuid& GraphId, const FGuid& Id) const;

	/** Forgets what the plays of a dialogue graph went through, as if it had never played. */
	void ForgetGraph(const FGuid& GraphId) { Memory.Graphs.Remove(GraphId); }

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