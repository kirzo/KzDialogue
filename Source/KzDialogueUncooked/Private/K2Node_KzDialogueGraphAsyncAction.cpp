// Copyright 2026 kirzo

#include "K2Node_KzDialogueGraphAsyncAction.h"
#include "KzDialogueK2Nodes.h"
#include "Graph/KzAsyncPlayDialogueGraph.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#define LOCTEXT_NAMESPACE "K2Node_KzDialogueGraphAsyncAction"

UK2Node_KzDialogueGraphAsyncAction::UK2Node_KzDialogueGraphAsyncAction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UK2Node_BaseAsyncTask only generates the Activate() call when this is set.
	ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UBlueprintAsyncActionBase, Activate);
}

FSlateIcon UK2Node_KzDialogueGraphAsyncAction::GetIconAndTint(FLinearColor& OutColor) const
{
	// The dialogue nodes' tint, with the graph asset's icon.
	KzDialogueK2Nodes::GetIcon(OutColor);
	return FSlateIcon(TEXT("KzDialogueEditorStyle"), TEXT("ClassIcon.KzDialogueGraph"));
}

FText UK2Node_KzDialogueGraphAsyncAction::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue|Graph");
}

FBlueprintNodeSignature UK2Node_KzDialogueGraphAsyncAction::GetSignature() const
{
	FBlueprintNodeSignature Signature = Super::GetSignature();
	Signature.AddKeyValue(ProxyFactoryFunctionName.ToString());
	return Signature;
}

void UK2Node_KzDialogueGraphAsyncAction::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	auto MakeSpawner = [NodeClass = GetClass()](FName FactoryName) -> UBlueprintNodeSpawner*
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass);
		check(Spawner);
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[FactoryName](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
			{
				UK2Node_KzDialogueGraphAsyncAction* Node = CastChecked<UK2Node_KzDialogueGraphAsyncAction>(NewNode);
				Node->ProxyFactoryClass = UKzAsyncPlayDialogueGraph::StaticClass();
				Node->ProxyFactoryFunctionName = FactoryName;
				Node->ProxyClass = UKzAsyncPlayDialogueGraph::StaticClass();
			});
		return Spawner;
	};

	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueGraph, PlayDialogueGraph)));
	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueGraph, ResumeDialogueGraph)));
}

#undef LOCTEXT_NAMESPACE