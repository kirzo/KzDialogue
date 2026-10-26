// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphConditions.h"
#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "KzDialogueMemorySubsystem.h"

bool FKzDialogueConditions::Evaluate(const FKzDialogueConditionContext& Context) const
{
	const bool bAny = Match == EKzDialogueConditionMatch::Any;
	for (const TInstancedStruct<FKzDialogueCondition>& Condition : Conditions)
	{
		const bool bMet = !Condition.IsValid() || Condition.Get().Evaluate(Context);
		if (bMet == bAny)
		{
			return bMet;
		}
	}
	return !bAny || Conditions.IsEmpty();
}

bool FKzDialogueCondition_Fact::IsMet(const FKzDialogueConditionContext& Context) const
{
	const UKzDialogueMemorySubsystem* Memory = Context.Instance.GetMemory();
	return Memory && Memory->GetFactCount(Fact) >= AtLeast;
}

bool FKzDialogueCondition_LineSeen::IsMet(const FKzDialogueConditionContext& Context) const
{
	const UKzDialogueMemorySubsystem* Memory = Context.Instance.GetMemory();
	return Memory && Memory->GetLineSeenCount(Line) >= AtLeast;
}

bool FKzDialogueCondition_Visited::IsMet(const FKzDialogueConditionContext& Context) const
{
	const UKzDialogueGraph* Graph = Context.Instance.GetGraph();
	const FGuid Id = Target.IsValid() || !Graph || Context.Node == INDEX_NONE ? Target : Graph->GetData().GetNodeGuid(Context.Node);
	return bEver ? Context.Instance.GetVisitCount(Id) >= AtLeast : Context.Instance.WasVisited(Id, false);
}

bool FKzDialogueCondition_InstigatorTags::IsMet(const FKzDialogueConditionContext& Context) const
{
	const IGameplayTagAssetInterface* Tags = Cast<IGameplayTagAssetInterface>(Context.Instance.GetContext().Instigator.Get());
	if (!Tags)
	{
		return false;
	}

	FGameplayTagContainer Owned;
	Tags->GetOwnedGameplayTags(Owned);
	return Query.Matches(Owned);
}

bool FKzDialogueCondition_Chance::IsMet(const FKzDialogueConditionContext& Context) const
{
	return FMath::FRand() < Probability;
}

bool FKzDialogueCondition_Blueprint::IsMet(const FKzDialogueConditionContext& Context) const
{
	// Blueprints have no const objects to take.
	return !Condition || Condition->IsMet(const_cast<UKzDialogueGraphInstance*>(&Context.Instance));
}

void FKzDialogueAction_AddFact::Execute(UKzDialogueGraphInstance& Instance) const
{
	if (UKzDialogueMemorySubsystem* Memory = Instance.GetMemory())
	{
		Memory->AddFact(Fact, Count);
	}
}

void FKzDialogueAction_SetFact::Execute(UKzDialogueGraphInstance& Instance) const
{
	if (UKzDialogueMemorySubsystem* Memory = Instance.GetMemory())
	{
		Memory->SetFactCount(Fact, Count);
	}
}

void FKzDialogueAction_RemoveFact::Execute(UKzDialogueGraphInstance& Instance) const
{
	if (UKzDialogueMemorySubsystem* Memory = Instance.GetMemory())
	{
		Memory->RemoveFact(Fact);
	}
}

void FKzDialogueAction_Blueprint::Execute(UKzDialogueGraphInstance& Instance) const
{
	if (Action)
	{
		Action->Execute(&Instance);
	}
}