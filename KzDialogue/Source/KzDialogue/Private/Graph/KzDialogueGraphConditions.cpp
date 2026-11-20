// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphConditions.h"
#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "KzDialogueAsset.h"
#include "KzDialogueMemorySubsystem.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraphConditions"

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
	const FGuid Id = Target.Id.IsValid() || !Graph || Context.Node == INDEX_NONE ? Target.Id : Graph->GetData().GetNodeGuid(Context.Node);
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

#if WITH_EDITOR
namespace
{
	/** "x3" after what needs more than once. */
	FText WithCount(const FText& Text, int32 AtLeast)
	{
		return AtLeast > 1 ? FText::Format(LOCTEXT("WithCount", "{0} x{1}"), Text, AtLeast) : Text;
	}
}

FText FKzDialogueCondition::GetDescription() const
{
	return LOCTEXT("Condition", "condition");
}

FText FKzDialogueCondition_Fact::GetDescription() const
{
	return WithCount(Fact.IsValid() ? FText::FromName(Fact.GetTagLeafName()) : LOCTEXT("NoFact", "(no fact)"), AtLeast);
}

FText FKzDialogueCondition_LineSeen::GetDescription() const
{
	const UKzDialogueAsset* Asset = Line.Asset.LoadSynchronous();
	const FKzDialogueLine* Found = Asset ? Asset->FindLine(Line.LineId) : nullptr;
	const FKzDialogueAlias* Alias = Asset && !Found ? Asset->FindAlias(Line.LineId) : nullptr;
	const FText Label = Found ? Found->GetDisplayLabel(32) : (Alias ? Alias->GetDisplayLabel() : LOCTEXT("NoLine", "(no line)"));
	return WithCount(FText::Format(LOCTEXT("Seen", "seen {0}"), Label), AtLeast);
}

FText FKzDialogueCondition_Visited::GetDescription() const
{
	const FText Text = Target.Id.IsValid() ? LOCTEXT("VisitedOther", "another node visited") : LOCTEXT("VisitedThis", "this node visited");
	return bEver ? WithCount(FText::Format(LOCTEXT("VisitedEver", "{0} in any play"), Text), AtLeast) : Text;
}

FText FKzDialogueCondition_InstigatorTags::GetDescription() const
{
	const FString& Description = Query.GetDescription();
	return Description.IsEmpty() ? LOCTEXT("NoQuery", "instigator tags") : FText::FromString(Description);
}

FText FKzDialogueCondition_Chance::GetDescription() const
{
	return FText::Format(LOCTEXT("Chance", "chance {0}"), FText::AsPercent(Probability));
}

FText FKzDialogueCondition_Group::GetDescription() const
{
	return LOCTEXT("Group", "group");
}

FText FKzDialogueCondition_Blueprint::GetDescription() const
{
	return Condition ? Condition->GetClass()->GetDisplayNameText() : LOCTEXT("NoBlueprint", "(no Blueprint)");
}
#endif

#undef LOCTEXT_NAMESPACE