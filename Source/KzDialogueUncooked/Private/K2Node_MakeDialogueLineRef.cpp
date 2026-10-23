// Copyright 2026 kirzo

#include "K2Node_MakeDialogueLineRef.h"
#include "KzDialogueK2Nodes.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_MakeStruct.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "K2Node_MakeDialogueLineRef"

const FName UK2Node_MakeDialogueLineRef::PN_ReturnValue = TEXT("ReturnValue");

// =======================================================================================
// Pin layout
// =======================================================================================

void UK2Node_MakeDialogueLineRef::AllocateDefaultPins()
{
	// Asset: a soft reference, matching the struct's Asset member type.
	UEdGraphPin* AssetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_SoftObject, UKzDialogueAsset::StaticClass(), KzDialogueK2Nodes::PN_Asset);
	AssetPin->PinFriendlyName = LOCTEXT("AssetPin", "Asset");

	// LineId: an FGuid struct, always connectable. The editor's pin factory gives it the line dropdown.
	UEdGraphPin* LineIdPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FGuid>::Get(), KzDialogueK2Nodes::PN_LineId);
	LineIdPin->PinFriendlyName = LOCTEXT("LineIdPin", "Line / Alias");

	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, FKzDialogueLineRef::StaticStruct(), PN_ReturnValue);
	ReturnPin->PinFriendlyName = LOCTEXT("ReturnPin", "Line Ref");

	Super::AllocateDefaultPins();
}

// =======================================================================================
// Cosmetics
// =======================================================================================

void UK2Node_MakeDialogueLineRef::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	KzDialogueK2Nodes::ClearStaleLineIds(this, Pin);
}

void UK2Node_MakeDialogueLineRef::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	KzDialogueK2Nodes::ClearStaleLineIds(this, Pin);
}

FText UK2Node_MakeDialogueLineRef::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return LOCTEXT("NodeTitle", "Make Dialogue Line Ref");
}

FText UK2Node_MakeDialogueLineRef::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Builds a FKzDialogueLineRef pointing at a specific line or alias inside a "
		"Dialogue Asset. When the Asset pin is unconnected and a literal asset is "
		"selected, the Line pin shows a dropdown of the asset's lines and aliases.");
}

FSlateIcon UK2Node_MakeDialogueLineRef::GetIconAndTint(FLinearColor& OutColor) const
{
	return KzDialogueK2Nodes::GetIcon(OutColor);
}

// =======================================================================================
// Menu registration
// =======================================================================================

void UK2Node_MakeDialogueLineRef::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		check(Spawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_MakeDialogueLineRef::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue");
}

// =======================================================================================
// Compilation: expand to a MakeStruct
// =======================================================================================

void UK2Node_MakeDialogueLineRef::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UK2Node_MakeStruct* MakeStructNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeStruct>(this, SourceGraph);
	MakeStructNode->StructType = FKzDialogueLineRef::StaticStruct();
	MakeStructNode->bMadeAfterOverridePinRemoval = true;
	MakeStructNode->AllocateDefaultPins();

	// Asset.
	if (UEdGraphPin* MakeAssetPin = MakeStructNode->FindPin(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, Asset)))
	{
		UEdGraphPin* OurAssetPin = FindPin(KzDialogueK2Nodes::PN_Asset);
		CompilerContext.MovePinLinksToIntermediate(*OurAssetPin, *MakeAssetPin);
		if (OurAssetPin->LinkedTo.Num() == 0)
		{
			MakeAssetPin->DefaultValue = OurAssetPin->DefaultValue;
		}
	}

	// LineId.
	if (UEdGraphPin* MakeLineIdPin = MakeStructNode->FindPin(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, LineId)))
	{
		UEdGraphPin* OurLineIdPin = FindPin(KzDialogueK2Nodes::PN_LineId);
		CompilerContext.MovePinLinksToIntermediate(*OurLineIdPin, *MakeLineIdPin);
		if (OurLineIdPin->LinkedTo.Num() == 0)
		{
			MakeLineIdPin->DefaultValue = OurLineIdPin->DefaultValue;
		}
	}

	// The MakeStruct's output takes over our return pin's links.
	if (UEdGraphPin* MakeStructReturnPin = MakeStructNode->FindPin(FKzDialogueLineRef::StaticStruct()->GetFName()))
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPin(PN_ReturnValue), *MakeStructReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE