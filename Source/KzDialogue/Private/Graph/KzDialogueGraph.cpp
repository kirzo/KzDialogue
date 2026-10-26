// Copyright 2026 kirzo

#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphNodes.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraph"

#if WITH_EDITOR
const UScriptStruct* UKzDialogueGraph::GetNodeStruct() const
{
	return FKzDialogueGraphNode::StaticStruct();
}

void UKzDialogueGraph::GetDefaultNodes(TArray<const UScriptStruct*>& OutNodes) const
{
	OutNodes.Add(FKzDialogueGraphNode_Entry::StaticStruct());
}

void UKzDialogueGraph::ValidateGraph(const FKzGraphData& InData, TArray<FKzValidationIssue>& OutIssues) const
{
	if (InData.GetEntries().IsEmpty())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("NoEntry", "The graph has no Entry to start from."), NAME_None));
	}

	TSet<FName> Entries;
	for (const FKzGraphEntry& Entry : InData.GetEntries())
	{
		bool bListed = false;
		Entries.Add(Entry.Name, &bListed);
		if (bListed)
		{
			FKzValidationIssue& Issue = OutIssues.Add_GetRef(FKzValidationIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("DuplicateEntry", "Two entries are named {0}."), FText::FromName(Entry.Name)), NAME_None));
			Issue.ContextId = InData.GetNodeGuid(Entry.Node);
		}
	}

	for (int32 Node = 0; Node < InData.NumNodes(); ++Node)
	{
		const FKzDialogueGraphNode_GoTo* GoTo = InData.GetNode<FKzDialogueGraphNode_GoTo>(Node);
		if (GoTo && !Entries.Contains(GoTo->Entry))
		{
			FKzValidationIssue& Issue = OutIssues.Add_GetRef(FKzValidationIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("GoToMissing", "Go To leads to {0}, but no entry has that name."), FText::FromName(GoTo->Entry)), NAME_None));
			Issue.ContextId = InData.GetNodeGuid(Node);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE