// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "KzDialogueGraphConditions.generated.h"

class UKzDialogueGraphInstance;

/** What a condition is checked for: the play, and the node the condition belongs to. */
struct FKzDialogueConditionContext
{
	const UKzDialogueGraphInstance& Instance;
	int32 Node = INDEX_NONE;
};

/** A node or a Choice option of the graph, picked from a list in the editor. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueGraphTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Target")
	FGuid Id;
};

/** Base of what dialogue graph nodes check, such as a fact or a line already seen. Asset data shared by every play: it keeps no state. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueCondition
{
	GENERATED_BODY()

	virtual ~FKzDialogueCondition() = default;

	/** Holds when the check fails. */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	bool Evaluate(const FKzDialogueConditionContext& Context) const { return IsMet(Context) != bInvert; }

#if WITH_EDITOR
	/** What the editor shows of it, Invert included. */
	FText Describe() const;
#endif

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const { return true; }

#if WITH_EDITOR
	virtual FText GetDescription() const;
#endif
};

UENUM()
enum class EKzDialogueConditionMatch : uint8
{
	/** Every condition holds. */
	All,

	/** At least one holds. */
	Any
};

/** Conditions checked together. Empty always holds. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueConditions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Conditions")
	EKzDialogueConditionMatch Match = EKzDialogueConditionMatch::All;

	UPROPERTY(EditAnywhere, Category = "Conditions", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FKzDialogueCondition>> Conditions;

	bool IsEmpty() const { return Conditions.IsEmpty(); }
	bool Evaluate(const FKzDialogueConditionContext& Context) const;

#if WITH_EDITOR
	/** The conditions joined by "and" or "or", for the editor. */
	FText GetDescription() const;
#endif
};

/** Base of what Action nodes do, such as noting a fact. Asset data shared by every play: it keeps no state. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueAction
{
	GENERATED_BODY()

	virtual ~FKzDialogueAction() = default;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const {}
};

/** A fact noted at least AtLeast times. */
USTRUCT(meta = (DisplayName = "Fact"))
struct KZDIALOGUE_API FKzDialogueCondition_Fact : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition")
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "1"))
	int32 AtLeast = 1;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** A line, or any line of an alias, presented at least AtLeast times in this game. */
USTRUCT(meta = (DisplayName = "Line Seen"))
struct KZDIALOGUE_API FKzDialogueCondition_LineSeen : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition")
	FKzDialogueLineRef Line;

	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "1"))
	int32 AtLeast = 1;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** A node of this graph, or an option of one of its Choices, gone through in this play, or in any play. */
USTRUCT(meta = (DisplayName = "Visited"))
struct KZDIALOGUE_API FKzDialogueCondition_Visited : public FKzDialogueCondition
{
	GENERATED_BODY()

	/** The node or the option; none checks the node holding the condition. */
	UPROPERTY(EditAnywhere, Category = "Condition")
	FKzDialogueGraphTarget Target;

	/** Counts every play of the graph, not only this one. */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bEver = false;

	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "1", EditCondition = "bEver"))
	int32 AtLeast = 1;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** The instigator's gameplay tags match, when it is an actor that exposes them (IGameplayTagAssetInterface). */
USTRUCT(meta = (DisplayName = "Instigator Tags"))
struct KZDIALOGUE_API FKzDialogueCondition_InstigatorTags : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition")
	FGameplayTagQuery Query;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** Holds by chance, each time it is checked. */
USTRUCT(meta = (DisplayName = "Chance"))
struct KZDIALOGUE_API FKzDialogueCondition_Chance : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition", meta = (ClampMin = "0", ClampMax = "1"))
	float Probability = 0.5f;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** Conditions with their own All or Any, to nest one inside another. */
USTRUCT(meta = (DisplayName = "Group"))
struct KZDIALOGUE_API FKzDialogueCondition_Group : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition")
	FKzDialogueConditions Conditions;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override { return Conditions.Evaluate(Context); }

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** Base of conditions written in Blueprint, used through the Blueprint condition. */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class KZDIALOGUE_API UKzDialogueBlueprintCondition : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	bool IsMet(UKzDialogueGraphInstance* Instance) const;
	virtual bool IsMet_Implementation(UKzDialogueGraphInstance* Instance) const { return true; }
};

/** A condition written in Blueprint. Only these cost an object each; the other conditions are plain data. */
USTRUCT(meta = (DisplayName = "Blueprint"))
struct KZDIALOGUE_API FKzDialogueCondition_Blueprint : public FKzDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Instanced, Category = "Condition")
	TObjectPtr<UKzDialogueBlueprintCondition> Condition;

protected:
	virtual bool IsMet(const FKzDialogueConditionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription() const override;
#endif
};

/** Notes a fact, adding Count to how many times it was noted. */
USTRUCT(meta = (DisplayName = "Add Fact"))
struct KZDIALOGUE_API FKzDialogueAction_AddFact : public FKzDialogueAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, Category = "Action")
	int32 Count = 1;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const override;
};

/** Sets how many times a fact was noted; zero forgets it. */
USTRUCT(meta = (DisplayName = "Set Fact"))
struct KZDIALOGUE_API FKzDialogueAction_SetFact : public FKzDialogueAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0"))
	int32 Count = 1;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const override;
};

/** Forgets a fact. */
USTRUCT(meta = (DisplayName = "Remove Fact"))
struct KZDIALOGUE_API FKzDialogueAction_RemoveFact : public FKzDialogueAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	FGameplayTag Fact;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const override;
};

/** Base of actions written in Blueprint, used through the Blueprint action. */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class KZDIALOGUE_API UKzDialogueBlueprintAction : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void Execute(UKzDialogueGraphInstance* Instance) const;
	virtual void Execute_Implementation(UKzDialogueGraphInstance* Instance) const {}
};

/** An action written in Blueprint. Only these cost an object each; the other actions are plain data. */
USTRUCT(meta = (DisplayName = "Blueprint"))
struct KZDIALOGUE_API FKzDialogueAction_Blueprint : public FKzDialogueAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Instanced, Category = "Action")
	TObjectPtr<UKzDialogueBlueprintAction> Action;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const override;
};