// Copyright 2026 kirzo

#include "Customizations/KzDialogueLinePinFactory.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "K2Node_PlayDialogueLine.h"
#include "KzDialogueAsset.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "SGraphPin.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "KzDialogueLinePinFactory"

// =======================================================================================
// Custom SGraphPin
// =======================================================================================

class SKzDialogueLineGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueLineGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SBox)
			.WidthOverride(220.f)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueLineGraphPin::BuildPickerContent)
					.ContentPadding(FMargin(2.f))
					.IsEnabled(this, &SKzDialogueLineGraphPin::IsDropdownAvailable)
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(this, &SKzDialogueLineGraphPin::GetCurrentLineLabel)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			];
	}

private:
	UK2Node_PlayDialogueLine* GetOwningNode() const
	{
		return GraphPinObj ? Cast<UK2Node_PlayDialogueLine>(GraphPinObj->GetOwningNode()) : nullptr;
	}

	UKzDialogueAsset* GetCurrentAsset() const
	{
		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		return Node ? Node->GetLiteralAsset() : nullptr;
	}

	bool IsDropdownAvailable() const
	{
		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		// Disabled when:
		//   - Asset pin is connected (LineId must come from caller).
		//   - LineId pin itself is connected (caller is providing the GUID).
		//   - No literal asset is selected.
		if (!Node || !Node->ShouldShowLineDropdown()) { return false; }
		if (GraphPinObj && GraphPinObj->LinkedTo.Num() > 0) { return false; }
		return true;
	}

	FText GetCurrentLineLabel() const
	{
		if (!GraphPinObj) { return LOCTEXT("InvalidPin", "(invalid)"); }

		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		if (!Node) { return LOCTEXT("InvalidNode", "(invalid)"); }

		// If the asset pin is connected we can't preview anything — show the GUID
		// directly (which is what the user typed/wired) so it's still inspectable.
		if (!Node->ShouldShowLineDropdown())
		{
			if (GraphPinObj->LinkedTo.Num() > 0)
			{
				return LOCTEXT("ConnectedHint", "Connected — set externally");
			}
			const FString DefaultStr = GraphPinObj->GetDefaultAsString();
			return DefaultStr.IsEmpty()
				? LOCTEXT("NoLineIdHint", "Set a Line GUID")
				: FText::FromString(DefaultStr);
		}

		// Asset is literal: render the resolved line label.
		FGuid LineId;
		FGuid::Parse(GraphPinObj->GetDefaultAsString(), LineId);

		UKzDialogueAsset* Asset = GetCurrentAsset();
		if (Asset && LineId.IsValid())
		{
			FKzDialogueLine Line;
			if (Asset->TryGetLineById(LineId, Line))
			{
				return Line.GetDisplayLabel(60);
			}
		}

		return LOCTEXT("PickLine", "Select line...");
	}

	TSharedRef<SWidget> BuildPickerContent()
	{
		UKzDialogueAsset* Asset = GetCurrentAsset();
		if (!Asset)
		{
			return SNew(SBox).Padding(8.f)
				[
					SNew(STextBlock).Text(LOCTEXT("NoAsset", "No literal asset selected."))
				];
		}

		return SNew(SBox).WidthOverride(320.f).HeightOverride(360.f)
			[
				SNew(SKzDialogueLinePicker)
					.Asset(Asset)
					.OnLinePicked(SKzDialogueLinePicker::FOnLinePicked::CreateSP(
						this, &SKzDialogueLineGraphPin::OnLinePicked))
			];
	}

	void OnLinePicked(FGuid InLineId, float /*DefaultDuration*/)
	{
		if (!GraphPinObj) { return; }

		const FString NewValue = InLineId.ToString(EGuidFormats::DigitsWithHyphens);
		if (NewValue == GraphPinObj->GetDefaultAsString()) { return; }

		const FScopedTransaction Transaction(LOCTEXT("PickLineTransaction", "Select dialogue line"));
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);

		FSlateApplication::Get().DismissAllMenus();
	}
};

// =======================================================================================
// Factory
// =======================================================================================

TSharedPtr<SGraphPin> FKzDialogueLinePinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (!InPin) { return nullptr; }

	// Only intercept the LineId pin of our specific K2Node.
	UK2Node_PlayDialogueLine* Node = Cast<UK2Node_PlayDialogueLine>(InPin->GetOwningNode());
	if (!Node) { return nullptr; }
	if (InPin->PinName != UK2Node_PlayDialogueLine::PN_LineId) { return nullptr; }

	return SNew(SKzDialogueLineGraphPin, InPin);
}

#undef LOCTEXT_NAMESPACE