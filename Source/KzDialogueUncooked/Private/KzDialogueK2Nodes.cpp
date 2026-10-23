// Copyright 2026 kirzo

#include "KzDialogueK2Nodes.h"
#include "K2Node_MakeDialogueLineList.h"
#include "K2Node_MakeDialogueLineRef.h"
#include "K2Node_PlayDialogueLine.h"
#include "K2Node_PlayDialogueLineAsync.h"
#include "KzDialogueAsset.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "KzDialogueK2Nodes"

namespace KzDialogueK2Nodes
{
	TFunction<void(UKzDialogueAsset*, const FGuid&)> OpenAssetAtEntry;

	UKzDialogueAsset* GetLiteralAsset(const UEdGraphNode* Node)
	{
		const UEdGraphPin* AssetPin = Node ? Node->FindPin(PN_Asset, EGPD_Input) : nullptr;
		if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return nullptr; }

		// A soft pin stores the path as its default value.
		if (AssetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			return AssetPin->DefaultValue.IsEmpty() ? nullptr : Cast<UKzDialogueAsset>(FSoftObjectPath(AssetPin->DefaultValue).TryLoad());
		}
		return Cast<UKzDialogueAsset>(AssetPin->DefaultObject);
	}

	bool IsLineIdPin(const UEdGraphPin* Pin)
	{
		if (!Pin || Pin->Direction != EGPD_Input) { return false; }

		const UEdGraphNode* Node = Pin->GetOwningNode();
		if (Node->IsA<UK2Node_MakeDialogueLineList>()) { return Pin->PinName.ToString().StartsWith(TEXT("Line_")); }
		return Pin->PinName == PN_LineId && (Node->IsA<UK2Node_PlayDialogueLine>() || Node->IsA<UK2Node_PlayDialogueLineAsync>() || Node->IsA<UK2Node_MakeDialogueLineRef>());
	}

	void ClearStaleLineIds(UEdGraphNode* Node, const UEdGraphPin* ChangedPin)
	{
		if (!Node || !ChangedPin || ChangedPin->PinName != PN_Asset) { return; }

		const UKzDialogueAsset* Asset = GetLiteralAsset(Node);
		for (UEdGraphPin* Pin : Node->Pins)
		{
			FGuid Id;
			if (!IsLineIdPin(Pin) || !FGuid::Parse(Pin->DefaultValue, Id) || !Id.IsValid()) { continue; }

			if (Asset && (Asset->FindLine(Id) || Asset->FindAlias(Id))) { continue; }

			Pin->Modify();
			Pin->GetSchema()->TrySetDefaultValue(*Pin, FString());
		}
	}

	void ValidateLineNode(const UEdGraphNode* Node, FCompilerResultsLog& MessageLog)
	{
		const UEdGraphPin* AssetPin = Node ? Node->FindPin(PN_Asset, EGPD_Input) : nullptr;
		if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return; }

		const UKzDialogueAsset* Asset = GetLiteralAsset(Node);
		if (!Asset)
		{
			MessageLog.Error(*LOCTEXT("NoAsset", "@@ has no dialogue asset.").ToString(), Node);
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsLineIdPin(Pin) || Pin->LinkedTo.Num() > 0) { continue; }

			FGuid Id;
			if (!FGuid::Parse(Pin->DefaultValue, Id) || !Id.IsValid())
			{
				MessageLog.Error(*LOCTEXT("NoLine", "@@: no line picked in @@.").ToString(), Node, Pin);
			}
			else if (!Asset->FindLine(Id) && !Asset->FindAlias(Id))
			{
				MessageLog.Error(*FText::Format(LOCTEXT("StaleLine", "@@: @@ names a line or alias that no longer exists in {0}."), FText::FromString(Asset->GetName())).ToString(), Node, Pin);
			}
		}
	}

	bool CanJumpToLine(const UEdGraphNode* Node)
	{
		// Without loading: the pin's literal is enough to know there is a target.
		const UEdGraphPin* AssetPin = Node ? Node->FindPin(PN_Asset, EGPD_Input) : nullptr;
		return OpenAssetAtEntry && AssetPin && AssetPin->LinkedTo.Num() == 0 && (AssetPin->DefaultObject || !AssetPin->DefaultValue.IsEmpty());
	}

	void JumpToLine(const UEdGraphNode* Node)
	{
		UKzDialogueAsset* Asset = GetLiteralAsset(Node);
		if (!Asset || !OpenAssetAtEntry) { return; }

		FGuid EntryId;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			FGuid PinId;
			if (IsLineIdPin(Pin) && Pin->LinkedTo.Num() == 0 && FGuid::Parse(Pin->DefaultValue, PinId) && PinId.IsValid())
			{
				EntryId = PinId;
				break;
			}
		}
		OpenAssetAtEntry(Asset, EntryId);
	}

	FSlateIcon GetIcon(FLinearColor& OutColor)
	{
		// FKzDialogueEditorStyle::BrandColor, which this module cannot reach.
		OutColor = FColor::FromHex(TEXT("#4A6EB6"));
		return FSlateIcon(TEXT("KzDialogueEditorStyle"), TEXT("Kz.Dialogue.Icon"));
	}
}

#undef LOCTEXT_NAMESPACE