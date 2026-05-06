// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineRefCustomization.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
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
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	// Resolve member handles by name. Member names must match the UPROPERTY() names
	// in FKzDialogueLineRef.
	AssetHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, Asset));
	LineIdHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineRef, LineId));

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
	// Row 1 — Asset (use the property's default editor: handles soft references,
	// browse-to / clear / use-selected buttons, etc.).
	if (AssetHandle.IsValid())
	{
		StructBuilder.AddProperty(AssetHandle.ToSharedRef());
	}

	// Row 2 — Line / Alias (custom dropdown).
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
		];
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
	if (!LineIdHandle.IsValid() || !InRef.IsValid()) { return; }

	const FString NewValue = InRef.Id.ToString(EGuidFormats::Digits);

	const FScopedTransaction Transaction(LOCTEXT("PickLineRefTransaction", "Pick dialogue line"));
	LineIdHandle->SetValueFromFormattedString(NewValue);

	FSlateApplication::Get().DismissAllMenus();
}

// =======================================================================================
// Helpers
// =======================================================================================

bool FKzDialogueLineRefCustomization::IsDropdownEnabled() const
{
	return GetCurrentAsset() != nullptr;
}

UKzDialogueAsset* FKzDialogueLineRefCustomization::GetCurrentAsset() const
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

	FKzDialogueLine Line;
	if (AssetPtr->TryGetLineById(Id, Line))
	{
		return Line.GetDisplayLabel(60);
	}

	FKzDialogueAlias Alias;
	if (AssetPtr->TryGetAliasById(Id, Alias))
	{
		return Alias.GetDisplayLabel();
	}

	return LOCTEXT("UnknownEntry", "(unknown)");
}

#undef LOCTEXT_NAMESPACE