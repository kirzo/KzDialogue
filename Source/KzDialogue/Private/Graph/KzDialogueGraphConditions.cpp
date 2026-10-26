// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphConditions.h"

bool FKzDialogueConditions::Evaluate(const UKzDialogueGraphInstance& Instance) const
{
	const bool bAny = Match == EKzDialogueConditionMatch::Any;
	for (const TInstancedStruct<FKzDialogueCondition>& Condition : Conditions)
	{
		const bool bMet = !Condition.IsValid() || Condition.Get().Evaluate(Instance);
		if (bMet == bAny)
		{
			return bMet;
		}
	}
	return !bAny || Conditions.IsEmpty();
}