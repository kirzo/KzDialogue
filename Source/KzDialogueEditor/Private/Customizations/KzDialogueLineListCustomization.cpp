// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineListCustomization.h"
#include "Customizations/KzDialogueLineFromAssetRowCustomizer.h"
#include "Widgets/SKzPropertyStack.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineListCustomization"

TSharedRef<IPropertyTypeCustomization> FKzDialogueLineListCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueLineListCustomization>();
}

void FKzDialogueLineListCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	AssetHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineList, Asset));
	LineIdsHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLineList, LineIds));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		[
			SNew(STextBlock)
				.Text_Lambda([this]() -> FText
					{
						if (!StructHandle.IsValid()) { return FText::GetEmpty(); }
						void* RawData = nullptr;
						if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
						{
							return FText::GetEmpty();
						}
						const FKzDialogueLineList* List = reinterpret_cast<const FKzDialogueLineList*>(RawData);
						if (!List || !List->IsValid())
						{
							return LOCTEXT("EmptyList", "(empty)");
						}
						return FText::Format(LOCTEXT("ListSummary", "({0} entries)"),
							FText::AsNumber(List->Num()));
					})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FKzDialogueLineListCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	if (AssetHandle.IsValid()) { StructBuilder.AddProperty(AssetHandle.ToSharedRef()); }

	if (LineIdsHandle.IsValid())
	{
		LineRowCustomizer = MakeShared<FKzDialogueLineFromAssetRowCustomizer>();

		// Asset comes from this struct's own Asset member. No speaker filter.
		// Aliases are shown.
		TWeakPtr<IPropertyHandle> WeakAssetHandle = AssetHandle;
		LineRowCustomizer->SetResolveAssetFn([WeakAssetHandle]() -> UKzDialogueAsset*
			{
				TSharedPtr<IPropertyHandle> Pin = WeakAssetHandle.Pin();
				if (!Pin.IsValid()) { return nullptr; }
				void* RawData = nullptr;
				if (Pin->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
				{
					return nullptr;
				}
				const TSoftObjectPtr<UKzDialogueAsset>* SoftPtr =
					reinterpret_cast<const TSoftObjectPtr<UKzDialogueAsset>*>(RawData);
				return (SoftPtr && !SoftPtr->IsNull()) ? SoftPtr->LoadSynchronous() : nullptr;
			});

		LineRowCustomizer->SetShowAliases(true);

		StructBuilder.AddCustomRow(LOCTEXT("LinesRowFilter", "Lines"))
			.WholeRowContent()
			[
				SNew(SBox)
					.MinDesiredHeight(160.f)
					.MaxDesiredHeight(420.f)
					[
						SNew(SKzPropertyStack, LineIdsHandle)
							.bAllowDuplicates(false)
							.ItemName(LOCTEXT("LineItemName", "Line"))
							.ItemNamePlural(LOCTEXT("LinePlural", "Lines"))
							.RowCustomizer(LineRowCustomizer)
					]
			];
	}
}

#undef LOCTEXT_NAMESPACE