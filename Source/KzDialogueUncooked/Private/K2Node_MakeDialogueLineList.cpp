// Copyright 2026 kirzo

#include "K2Node_MakeDialogueLineList.h"
#include "KzDialogueK2Nodes.h"

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
#include "ScopedTransaction.h"
#include "ToolMenu.h"

#define LOCTEXT_NAMESPACE "K2Node_MakeDialogueLineList"

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
	// Asset: a soft reference, matching the struct's Asset member type.
	UEdGraphPin* AssetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_SoftObject, UKzDialogueAsset::StaticClass(), KzDialogueK2Nodes::PN_Asset);
	AssetPin->PinFriendlyName = LOCTEXT("AssetPin", "Asset");

	// Line pins: FGuid structs, always connectable. The editor's pin factory gives them the line dropdown.
	for (int32 Index = 0; Index < NumLinePins; ++Index)
	{
		UEdGraphPin* LinePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FGuid>::Get(), MakeLinePinName(Index));
		LinePin->PinFriendlyName = FText::Format(LOCTEXT("LinePinFriendly", "Line {0}"), FText::AsNumber(Index));
	}

	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, FKzDialogueLineList::StaticStruct(), PN_ReturnValue);
	ReturnPin->PinFriendlyName = LOCTEXT("ReturnPin", "Line List");

	Super::AllocateDefaultPins();
}

// =======================================================================================
// Cosmetics
// =======================================================================================

void UK2Node_MakeDialogueLineList::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	KzDialogueK2Nodes::ClearStaleLineIds(this, Pin);
}

void UK2Node_MakeDialogueLineList::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	KzDialogueK2Nodes::ClearStaleLineIds(this, Pin);
}

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
	return KzDialogueK2Nodes::GetIcon(OutColor);
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
	if (!KzDialogueK2Nodes::IsLineIdPin(Pin) || NumLinePins <= 1) return;

	Modify();

	Pin->BreakAllPinLinks();
	RemovePin(Pin);
	--NumLinePins;

	// Renumber the remaining line pins so names stay contiguous.
	int32 Index = 0;
	for (UEdGraphPin* Existing : Pins)
	{
		if (KzDialogueK2Nodes::IsLineIdPin(Existing))
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

	if (KzDialogueK2Nodes::IsLineIdPin(Context->Pin))
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
					const FScopedTransaction Transaction(LOCTEXT("RemoveLinePinTransaction", "Remove Line Pin"));
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
				const FScopedTransaction Transaction(LOCTEXT("AddLinePinTransaction", "Add Line Pin"));
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

void UK2Node_MakeDialogueLineList::GetLinePins(TArray<UEdGraphPin*>& OutPins) const
{
	OutPins.Reset();
	for (UEdGraphPin* Pin : Pins)
	{
		if (KzDialogueK2Nodes::IsLineIdPin(Pin))
		{
			OutPins.Add(Pin);
		}
	}
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
		UEdGraphPin* OurAssetPin = FindPin(KzDialogueK2Nodes::PN_Asset);
		CompilerContext.MovePinLinksToIntermediate(*OurAssetPin, *MakeAssetPin);
		if (OurAssetPin->LinkedTo.Num() == 0)
		{
			MakeAssetPin->DefaultValue = OurAssetPin->DefaultValue;
		}
	}

	// LineIds: the line pins gathered through an intermediate MakeArray.
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

	// The MakeStruct's output takes over our return pin's links.
	if (UEdGraphPin* MakeStructReturnPin = MakeStructNode->FindPin(FKzDialogueLineList::StaticStruct()->GetFName()))
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPin(PN_ReturnValue), *MakeStructReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE