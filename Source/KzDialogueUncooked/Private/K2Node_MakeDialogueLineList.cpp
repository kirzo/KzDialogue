// Copyright 2026 kirzo

#include "K2Node_MakeDialogueLineList.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ToolMenu.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_MakeDialogueLineList"

const FName UK2Node_MakeDialogueLineList::PN_Asset = TEXT("Asset");
const FName UK2Node_MakeDialogueLineList::PN_ReturnValue = TEXT("ReturnValue");

FName UK2Node_MakeDialogueLineList::MakeLinePinName(int32 Index)
{
	return FName(*FString::Printf(TEXT("Line_%d"), Index));
}

// =======================================================================================
// Pin layout
// =======================================================================================

void UK2Node_MakeDialogueLineList::AllocateDefaultPins()
{
	// Asset — soft reference, matches the struct's Asset member type.
	UEdGraphPin* AssetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_SoftObject, UKzDialogueAsset::StaticClass(), PN_Asset);
	AssetPin->PinFriendlyName = LOCTEXT("AssetPin", "Asset");

	// Line pins — typed as FGuid struct, always connectable. The pin factory in the
	// editor module replaces their default widget with the dropdown when appropriate.
	for (int32 Index = 0; Index < NumLinePins; ++Index)
	{
		UEdGraphPin* LinePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FGuid>::Get(), MakeLinePinName(Index));
		LinePin->PinFriendlyName = FText::Format(LOCTEXT("LinePinFriendly", "Line {0}"), FText::AsNumber(Index));
	}

	// Return value — FKzDialogueLineList.
	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, FKzDialogueLineList::StaticStruct(), PN_ReturnValue);
	ReturnPin->PinFriendlyName = LOCTEXT("ReturnPin", "Line List");

	Super::AllocateDefaultPins();
}

// =======================================================================================
// Cosmetics
// =======================================================================================

FText UK2Node_MakeDialogueLineList::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return LOCTEXT("NodeTitle", "Make Dialogue Line List");
}

FText UK2Node_MakeDialogueLineList::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Builds a FKzDialogueLineList from one Dialogue Asset and a variable number of "
		"lines or aliases (right-click the node to add or remove line pins). When the "
		"Asset pin is unconnected and a literal asset is selected, every Line pin shows "
		"a dropdown of the asset's lines and aliases.");
}

FSlateIcon UK2Node_MakeDialogueLineList::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FColor::FromHex(TEXT("#4A6EB6"));
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment");
}

// =======================================================================================
// Reactions
// =======================================================================================

void UK2Node_MakeDialogueLineList::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// The pin factory's widget reads ShouldShowLineDropdown() so a refresh is enough.
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_MakeDialogueLineList::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// Literal asset changed: the dropdown's option list depends on it.
		GetGraph()->NotifyGraphChanged();
	}
}

// =======================================================================================
// Dynamic line pins
// =======================================================================================

void UK2Node_MakeDialogueLineList::AddInputPin()
{
	Modify();

	UEdGraphPin* LinePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FGuid>::Get(), MakeLinePinName(NumLinePins));
	LinePin->PinFriendlyName = FText::Format(LOCTEXT("LinePinFriendly", "Line {0}"), FText::AsNumber(NumLinePins));
	++NumLinePins;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

void UK2Node_MakeDialogueLineList::RemoveLinePin(UEdGraphPin* Pin)
{
	if (!Pin || !IsLinePin(Pin) || NumLinePins <= 1) return;

	Modify();

	Pin->BreakAllPinLinks();
	RemovePin(Pin);
	--NumLinePins;

	// Renumber the remaining line pins so names stay contiguous.
	int32 Index = 0;
	for (UEdGraphPin* Existing : Pins)
	{
		if (IsLinePin(Existing))
		{
			Existing->PinName = MakeLinePinName(Index);
			Existing->PinFriendlyName = FText::Format(LOCTEXT("LinePinFriendly", "Line {0}"), FText::AsNumber(Index));
			++Index;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

void UK2Node_MakeDialogueLineList::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (Context->bIsDebugging) return;

	FToolMenuSection& Section = Menu->AddSection("K2NodeMakeDialogueLineList", LOCTEXT("ContextMenuHeader", "Line List"));

	if (Context->Pin && IsLinePin(Context->Pin))
	{
		if (NumLinePins > 1)
		{
			Section.AddMenuEntry(
				"RemoveLinePin",
				LOCTEXT("RemoveLinePin", "Remove line pin"),
				LOCTEXT("RemoveLinePinTooltip", "Remove this line from the list."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([MutableThis = const_cast<UK2Node_MakeDialogueLineList*>(this), Pin = const_cast<UEdGraphPin*>(Context->Pin)]()
				{
					MutableThis->RemoveLinePin(Pin);
				})));
		}
	}
	else
	{
		Section.AddMenuEntry(
			"AddLinePin",
			LOCTEXT("AddLinePin", "Add line pin"),
			LOCTEXT("AddLinePinTooltip", "Add another line to the list."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([MutableThis = const_cast<UK2Node_MakeDialogueLineList*>(this)]()
			{
				MutableThis->AddInputPin();
			})));
	}
}

// =======================================================================================
// Menu registration
// =======================================================================================

void UK2Node_MakeDialogueLineList::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		check(Spawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_MakeDialogueLineList::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue");
}

// =======================================================================================
// Pin accessors
// =======================================================================================

UEdGraphPin* UK2Node_MakeDialogueLineList::GetAssetPin() const { return FindPin(PN_Asset); }

bool UK2Node_MakeDialogueLineList::IsLinePin(const UEdGraphPin* Pin) const
{
	return Pin && Pin->Direction == EGPD_Input && Pin->PinName.ToString().StartsWith(TEXT("Line_"));
}

void UK2Node_MakeDialogueLineList::GetLinePins(TArray<UEdGraphPin*>& OutPins) const
{
	OutPins.Reset();
	for (UEdGraphPin* Pin : Pins)
	{
		if (IsLinePin(Pin))
		{
			OutPins.Add(Pin);
		}
	}
}

bool UK2Node_MakeDialogueLineList::ShouldShowLineDropdown() const
{
	const UEdGraphPin* AssetPin = GetAssetPin();
	if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return false; }
	return GetLiteralAsset() != nullptr;
}

UKzDialogueAsset* UK2Node_MakeDialogueLineList::GetLiteralAsset() const
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
// Compilation: expand to MakeArray + MakeStruct
// =======================================================================================

void UK2Node_MakeDialogueLineList::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UK2Node_MakeStruct* MakeStructNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeStruct>(this, SourceGraph);
	MakeStructNode->StructType = FKzDialogueLineList::StaticStruct();
	MakeStructNode->bMadeAfterOverridePinRemoval = true;
	MakeStructNode->AllocateDefaultPins();

	// Asset.
	if (UEdGraphPin* MakeAssetPin = MakeStructNode->FindPin(GET_MEMBER_NAME_CHECKED(FKzDialogueLineList, Asset)))
	{
		UEdGraphPin* OurAssetPin = GetAssetPin();
		CompilerContext.MovePinLinksToIntermediate(*OurAssetPin, *MakeAssetPin);
		if (OurAssetPin->LinkedTo.Num() == 0)
		{
			MakeAssetPin->DefaultValue = OurAssetPin->DefaultValue;
		}
	}

	// LineIds — gather the line pins through an intermediate MakeArray.
	if (UEdGraphPin* MakeLineIdsPin = MakeStructNode->FindPin(GET_MEMBER_NAME_CHECKED(FKzDialogueLineList, LineIds)))
	{
		UK2Node_MakeArray* MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeArrayNode->NumInputs = NumLinePins;
		MakeArrayNode->AllocateDefaultPins();

		// Connect the wildcard array output to the typed struct member first, so the
		// MakeArray adopts FGuid and propagates it to its input pins.
		UEdGraphPin* ArrayOut = MakeArrayNode->GetOutputPin();
		CompilerContext.GetSchema()->TryCreateConnection(ArrayOut, MakeLineIdsPin);
		MakeArrayNode->PinConnectionListChanged(ArrayOut);

		TArray<UEdGraphPin*> ArrayInputs;
		for (UEdGraphPin* Pin : MakeArrayNode->Pins)
		{
			if (Pin->Direction == EGPD_Input)
			{
				ArrayInputs.Add(Pin);
			}
		}

		TArray<UEdGraphPin*> LinePins;
		GetLinePins(LinePins);

		for (int32 Index = 0; Index < LinePins.Num() && Index < ArrayInputs.Num(); ++Index)
		{
			CompilerContext.MovePinLinksToIntermediate(*LinePins[Index], *ArrayInputs[Index]);
			if (LinePins[Index]->LinkedTo.Num() == 0)
			{
				ArrayInputs[Index]->DefaultValue = LinePins[Index]->DefaultValue;
			}
		}
	}

	// Return value — connect MakeStruct's struct output to our return pin's targets.
	if (UEdGraphPin* MakeStructReturnPin = MakeStructNode->FindPin(FKzDialogueLineList::StaticStruct()->GetFName()))
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPin(PN_ReturnValue), *MakeStructReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE