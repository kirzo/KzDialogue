// Copyright 2026 kirzo

#include "K2Node_KzDialogueAsyncAction.h"
#include "KzDialogueAsyncActions.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_KzDialogueAsyncAction"

UK2Node_KzDialogueAsyncAction::UK2Node_KzDialogueAsyncAction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UK2Node_BaseAsyncTask only generates the Activate() call when this is set (it defaults to
	// None there; UK2Node_AsyncAction is who normally sets it). Without it the action never runs.
	ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UBlueprintAsyncActionBase, Activate);
}

FSlateIcon UK2Node_KzDialogueAsyncAction::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FColor::FromHex(TEXT("#4A6EB6")); // Match the dialogue asset color.
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment");
}

FText UK2Node_KzDialogueAsyncAction::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue");
}

void UK2Node_KzDialogueAsyncAction::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	auto MakeSpawner = [NodeClass = GetClass()](UClass* FactoryClass, FName FactoryName) -> UBlueprintNodeSpawner*
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass);
		check(Spawner);
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[FactoryClass, FactoryName](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
			{
				UK2Node_KzDialogueAsyncAction* Node = CastChecked<UK2Node_KzDialogueAsyncAction>(NewNode);
				Node->ProxyFactoryClass = FactoryClass;
				Node->ProxyFactoryFunctionName = FactoryName;
				Node->ProxyClass = FactoryClass;
			});
		return Spawner;
	};

	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(UKzAsyncPlayDialogueLine::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueLine, PlayDialogueLine)));
	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(UKzAsyncPlayDialogueLineList::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueLineList, PlayDialogueLineList)));
	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(UKzAsyncPlayDialogueLineRefs::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueLineRefs, PlayDialogueLineRefs)));
	ActionRegistrar.AddBlueprintAction(ActionKey, MakeSpawner(UKzAsyncPlayDialogueAsset::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueAsset, PlayDialogueAsset)));
}

#undef LOCTEXT_NAMESPACE