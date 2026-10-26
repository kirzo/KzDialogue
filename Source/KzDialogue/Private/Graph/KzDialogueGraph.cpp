// Copyright 2026 kirzo

#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphNodes.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraph"

void UKzDialogueGraph::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) && !GraphId.IsValid())
	{
		GraphId = FGuid::NewGuid();
	}
}

#if WITH_EDITOR
void UKzDialogueGraph::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		GraphId = FGuid::NewGuid();
	}
}

const UScriptStruct* UKzDialogueGraph::GetNodeStruct() const
{
	return FKzDialogueGraphNode::StaticStruct();
}

void UKzDialogueGraph::GetDefaultNodes(TArray<const UScriptStruct*>& OutNodes) const
{
	OutNodes.Add(FKzDialogueGraphNode_Start::StaticStruct());
}

void UKzDialogueGraph::ValidateGraph(const FKzGraphData& InData, TArray<FKzValidationIssue>& OutIssues) const
{
	if (InData.GetEntries().IsEmpty())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("NoEntry", "The graph has no Entry to start from."), NAME_None));
	}
	if (Lines.IsNull())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning, LOCTEXT("NoLines", "The graph has no lines asset for its nodes to pick and add lines from."), NAME_None));
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
		if (GoTo && !Entries.Contains(GoTo->Entry.Name))
		{
			FKzValidationIssue& Issue = OutIssues.Add_GetRef(FKzValidationIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("GoToMissing", "Go To leads to {0}, but no entry has that name."), FText::FromName(GoTo->Entry.Name)), NAME_None));
			Issue.ContextId = InData.GetNodeGuid(Node);
		}

		const FKzDialogueGraphNode_SubGraph* Sub = InData.GetNode<FKzDialogueGraphNode_SubGraph>(Node);
		if (Sub && Sub->Graph == this)
		{
			FKzValidationIssue& Issue = OutIssues.Add_GetRef(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("SubGraphSelf", "A Sub Graph plays this same graph; use Go To to go back to one of its entries."), NAME_None));
			Issue.ContextId = InData.GetNodeGuid(Node);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE