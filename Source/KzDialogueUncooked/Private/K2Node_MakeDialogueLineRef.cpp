// Copyright 2026 kirzo

#include "K2Node_MakeDialogueLineRef.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_MakeStruct.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_MakeDialogueLineRef"

const FName UK2Node_MakeDialogueLineRef::PN_Asset = TEXT("Asset");
const FName UK2Node_MakeDialogueLineRef::PN_LineId = TEXT("LineId");
const FName UK2Node_MakeDialogueLineRef::PN_ReturnValue = TEXT("ReturnValue");

// =======================================================================================
// Pin layout
// =======================================================================================

void UK2Node_MakeDialogueLineRef::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();

	// Asset — soft reference, matches the struct's Asset member type.
	UEdGraphPin* AssetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_SoftObject, UKzDialogueAsset::StaticClass(), PN_Asset);
	AssetPin->PinFriendlyName = LOCTEXT("AssetPin", "Asset");

	// LineId — typed as FGuid struct, always connectable. The pin factory in the
	// editor module replaces its default widget with the dropdown when appropriate.
	UEdGraphPin* LineIdPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FGuid>::Get(), PN_LineId);
	LineIdPin->PinFriendlyName = LOCTEXT("LineIdPin", "Line / Alias");

	// Return value — FKzDialogueLineRef.
	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, FKzDialogueLineRef::StaticStruct(), PN_ReturnValue);
	ReturnPin->PinFriendlyName = LOCTEXT("ReturnPin", "Line Ref");

	Super::AllocateDefaultPins();
}

// =======================================================================================
// Cosmetics
// =======================================================================================

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
	OutColor = FColor::FromHex(TEXT("#4A6EB6"));
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment");
}

// =======================================================================================
// Reactions
// =======================================================================================

void UK2Node_MakeDialogueLineRef::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// The pin factory's widget reads ShouldShowLineDropdown() so a refresh is enough.
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_MakeDialogueLineRef::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// Literal asset changed: the dropdown's option list depends on it.
		GetGraph()->NotifyGraphChanged();
	}
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
// Pin accessors
// =======================================================================================

UEdGraphPin* UK2Node_MakeDialogueLineRef::GetAssetPin() const { return FindPin(PN_Asset); }
UEdGraphPin* UK2Node_MakeDialogueLineRef::GetLineIdPin() const { return FindPin(PN_LineId); }

bool UK2Node_MakeDialogueLineRef::ShouldShowLineDropdown() const
{
	const UEdGraphPin* AssetPin = GetAssetPin();
	if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return false; }
	return GetLiteralAsset() != nullptr;
}

UKzDialogueAsset* UK2Node_MakeDialogueLineRef::GetLiteralAsset() const
{
	const UEdGraphPin* AssetPin = GetAssetPin();
	if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return nullptr; }

	// PC_SoftObject pins store the path as DefaultValue. Resolve it synchronously —
	// this only runs in the editor while the user is inspecting/editing the node.
	const FString& Path = AssetPin->DefaultValue;
	if (Path.IsEmpty()) { return nullptr; }

	const FSoftObjectPath SoftPath(Path);
	return Cast<UKzDialogueAsset>(SoftPath.TryLoad());
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
		UEdGraphPin* OurAssetPin = GetAssetPin();
		CompilerContext.MovePinLinksToIntermediate(*OurAssetPin, *MakeAssetPin);
		if (OurAssetPin->LinkedTo.Num() == 0)
		{
			MakeAssetPin->DefaultValue = OurAssetPin->DefaultValue;
		}
	}

	// LineId.
	if (UEdGraphPin* MakeLineIdPin = MakeStructNode->FindPin(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, LineId)))
	{
		UEdGraphPin* OurLineIdPin = GetLineIdPin();
		CompilerContext.MovePinLinksToIntermediate(*OurLineIdPin, *MakeLineIdPin);
		if (OurLineIdPin->LinkedTo.Num() == 0)
		{
			MakeLineIdPin->DefaultValue = OurLineIdPin->DefaultValue;
		}
	}

	// Return value — connect MakeStruct's struct output to our return pin's targets.
	if (UEdGraphPin* MakeStructReturnPin = MakeStructNode->FindPin(FKzDialogueLineRef::StaticStruct()->GetFName()))
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPin(PN_ReturnValue), *MakeStructReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE