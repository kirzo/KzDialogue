// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineRefCustomization.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "Graph/KzDialogueGraph.h"
#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"
#include "KzGraphEditorUtils.h"

#include "DetailWidgetRow.h"
#include "EdGraph/EdGraphNode.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineRefCustomization"

TSharedRef<IPropertyTypeCustomization> FKzDialogueLineRefCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueLineRefCustomization>();
}

// =======================================================================================
// Header row
// =======================================================================================

void FKzDialogueLineRefCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();
	bInlineLine = StructPropertyHandle->HasMetaData(TEXT("InlineLine"));

	// Resolve member handles by name. Member names must match the UPROPERTY() names
	// in FKzDialogueLineRef.
	AssetHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, Asset));
	LineIdHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, LineId));
	if (AssetHandle.IsValid())
	{
		AssetHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FKzDialogueLineRefCustomization::ClearStaleLineId));
	}

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		[
			SNew(STextBlock)
				.Text_Lambda([this]() { return GetCurrentLineLabel(); })
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

// =======================================================================================
// Child rows
// =======================================================================================

void FKzDialogueLineRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	// Row 1: Asset (use the property's default editor: handles soft references,
	// browse-to / clear / use-selected buttons, etc.).
	if (AssetHandle.IsValid())
	{
		StructBuilder.AddProperty(AssetHandle.ToSharedRef());
	}

	// Row 2: Line / Alias (custom dropdown).
	StructBuilder.AddCustomRow(LOCTEXT("LineRowFilter", "Line Alias"))
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("LineRowName", "Line / Alias"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &FKzDialogueLineRefCustomization::BuildPickerMenu)
					.ContentPadding(FMargin(2.f))
					.IsEnabled_Lambda([this]() { return IsDropdownEnabled(); })
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text_Lambda([this]() { return GetCurrentLineLabel(); })
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.f))
					.ToolTipText(LOCTEXT("NewLineTip", "Adds a new line to the asset and picks it."))
					.Visibility(bInlineLine ? EVisibility::Visible : EVisibility::Collapsed)
					.IsEnabled_Lambda([this]() { return IsDropdownEnabled(); })
					.OnClicked(this, &FKzDialogueLineRefCustomization::OnNewLineClicked)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
			]
		];

	// The line itself, edited on its asset: the collapsed Lines row only roots the element's handle.
	UKzDialogueAsset* Asset = bInlineLine ? GetCurrentAsset() : nullptr;
	const FGuid Id = GetCurrentGuid();
	const int32 LineIndex = Asset ? Asset->Lines.IndexOfByPredicate([&Id](const FKzDialogueLine& Line) { return Line.LineId == Id; }) : INDEX_NONE;
	if (LineIndex == INDEX_NONE)
	{
		return;
	}

	IDetailPropertyRow* LinesRow = StructBuilder.AddExternalObjectProperty(TArray<UObject*>{ Asset }, GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Lines));
	const TSharedPtr<IPropertyHandle> LinesHandle = LinesRow ? LinesRow->GetPropertyHandle() : nullptr;
	const TSharedPtr<IPropertyHandle> LineHandle = LinesHandle.IsValid() ? LinesHandle->GetChildHandle(LineIndex) : nullptr;
	if (LineHandle.IsValid())
	{
		LinesRow->Visibility(EVisibility::Collapsed);
		StructBuilder.AddProperty(LineHandle.ToSharedRef())
			.DisplayName(LOCTEXT("InlineLineName", "Contents"))
			.ShouldAutoExpand(true);
	}
}

// =======================================================================================
// Picker
// =======================================================================================

TSharedRef<SWidget> FKzDialogueLineRefCustomization::BuildPickerMenu()
{
	UKzDialogueAsset* AssetPtr = GetCurrentAsset();
	if (!AssetPtr)
	{
		return SNew(SBox).Padding(8.f)
			[
				SNew(STextBlock).Text(LOCTEXT("NoAsset", "Select an asset first."))
			];
	}

	return SNew(SBox).WidthOverride(320.f).HeightOverride(360.f)
		[
			SNew(SKzDialogueLinePicker)
				.Asset(AssetPtr)
				.OnEntryPicked(SKzDialogueLinePicker::FOnEntryPicked::CreateSP(
					this, &FKzDialogueLineRefCustomization::OnEntryPicked))
		];
}

void FKzDialogueLineRefCustomization::OnEntryPicked(FKzDialogueAssetReference InRef, float /*Duration*/)
{
	UKzDialogueAsset* Asset = GetCurrentAsset();
	if (!LineIdHandle.IsValid() || !InRef.IsValid() || !Asset) { return; }

	const FScopedTransaction Transaction(LOCTEXT("PickLineRefTransaction", "Pick dialogue line"));
	SetEntry(*Asset, InRef.Id);

	FSlateApplication::Get().DismissAllMenus();
}

FReply FKzDialogueLineRefCustomization::OnNewLineClicked()
{
	UKzDialogueAsset* Asset = GetCurrentAsset();
	if (!LineIdHandle.IsValid() || !Asset) { return FReply::Handled(); }

	const FScopedTransaction Transaction(LOCTEXT("NewLineTransaction", "New dialogue line"));
	Asset->Modify();
	const FGuid Id = FGuid::NewGuid();
	Asset->Lines.AddDefaulted_GetRef().LineId = Id;
	Asset->PostEditChange();
	SetEntry(*Asset, Id);
	return FReply::Handled();
}

void FKzDialogueLineRefCustomization::SetEntry(UKzDialogueAsset& Asset, const FGuid& Id)
{
	// The game reads only the ref, so a fallback asset is stored on it.
	if (AssetHandle.IsValid() && !GetRefAsset())
	{
		AssetHandle->SetValueFromFormattedString(Asset.GetPathName());
	}
	LineIdHandle->SetValueFromFormattedString(Id.ToString(EGuidFormats::Digits));

	if (bInlineLine && PropertyUtilities.IsValid())
	{
		PropertyUtilities->RequestForceRefresh();
	}
}

// =======================================================================================
// Helpers
// =======================================================================================

void FKzDialogueLineRefCustomization::ClearStaleLineId()
{
	FString IdString;
	FGuid Id;
	if (!LineIdHandle.IsValid() || LineIdHandle->GetValueAsFormattedString(IdString) != FPropertyAccess::Success || !FGuid::Parse(IdString, Id) || !Id.IsValid()) { return; }

	const UKzDialogueAsset* Asset = GetRefAsset();
	if (!Asset || (!Asset->FindLine(Id) && !Asset->FindAlias(Id)))
	{
		LineIdHandle->SetValueFromFormattedString(FGuid().ToString(EGuidFormats::Digits));
	}

	if (bInlineLine && PropertyUtilities.IsValid())
	{
		PropertyUtilities->RequestForceRefresh();
	}
}

bool FKzDialogueLineRefCustomization::IsDropdownEnabled() const
{
	return GetCurrentAsset() != nullptr;
}

UKzDialogueAsset* FKzDialogueLineRefCustomization::GetCurrentAsset() const
{
	UKzDialogueAsset* Asset = GetRefAsset();
	return Asset ? Asset : GetGraphLinesAsset();
}

UKzDialogueAsset* FKzDialogueLineRefCustomization::GetGraphLinesAsset() const
{
	if (!PropertyUtilities.IsValid()) { return nullptr; }
	for (const TWeakObjectPtr<UObject>& Object : PropertyUtilities->GetSelectedObjects())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(Object.Get());
		if (const UKzDialogueGraph* Graph = Node ? Cast<UKzDialogueGraph>(KzGraphEditor::GetGraphAsset(Node->GetGraph())) : nullptr)
		{
			return Graph->Lines.LoadSynchronous();
		}
	}
	return nullptr;
}

UKzDialogueAsset* FKzDialogueLineRefCustomization::GetRefAsset() const
{
	if (!AssetHandle.IsValid()) { return nullptr; }

	// SoftObjectPtr property: read raw value, resolve to UObject*. We only return a
	// loaded pointer; if the asset hasn't been loaded yet, force-load it (the user is
	// in the editor and waiting for a UI response, so synchronous load is fine).
	void* RawData = nullptr;
	if (AssetHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return nullptr;
	}

	const TSoftObjectPtr<UKzDialogueAsset>* SoftPtr =
		reinterpret_cast<const TSoftObjectPtr<UKzDialogueAsset>*>(RawData);
	if (!SoftPtr || SoftPtr->IsNull()) { return nullptr; }

	return SoftPtr->LoadSynchronous();
}

FGuid FKzDialogueLineRefCustomization::GetCurrentGuid() const
{
	FGuid Result;
	if (LineIdHandle.IsValid())
	{
		FString AsString;
		LineIdHandle->GetValueAsFormattedString(AsString);
		FGuid::Parse(AsString, Result);
	}
	return Result;
}

FText FKzDialogueLineRefCustomization::GetCurrentLineLabel() const
{
	UKzDialogueAsset* AssetPtr = GetCurrentAsset();
	if (!AssetPtr)
	{
		return LOCTEXT("NoAssetLabel", "(no asset)");
	}

	const FGuid Id = GetCurrentGuid();
	if (!Id.IsValid())
	{
		return LOCTEXT("PickLine", "Select line or alias...");
	}

	if (const FKzDialogueLine* Line = AssetPtr->FindLine(Id))
	{
		return Line->GetDisplayLabel(60);
	}
	if (const FKzDialogueAlias* Alias = AssetPtr->FindAlias(Id))
	{
		return Alias->GetDisplayLabel();
	}

	return LOCTEXT("UnknownEntry", "(unknown)");
}

#undef LOCTEXT_NAMESPACE