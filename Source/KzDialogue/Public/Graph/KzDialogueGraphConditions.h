// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "KzDialogueGraphConditions.generated.h"

class UKzDialogueGraphInstance;

/** Base of what dialogue graph nodes check, such as a fact or a line already seen. Asset data shared by every play: it keeps no state. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueCondition
{
	GENERATED_BODY()

	virtual ~FKzDialogueCondition() = default;

	/** Holds when the check fails. */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	bool Evaluate(const UKzDialogueGraphInstance& Instance) const { return IsMet(Instance) != bInvert; }

protected:
	virtual bool IsMet(const UKzDialogueGraphInstance& Instance) const { return true; }
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
	bool Evaluate(const UKzDialogueGraphInstance& Instance) const;
};

/** Base of what Action nodes do, such as noting a fact. Asset data shared by every play: it keeps no state. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueAction
{
	GENERATED_BODY()

	virtual ~FKzDialogueAction() = default;

	virtual void Execute(UKzDialogueGraphInstance& Instance) const {}
};