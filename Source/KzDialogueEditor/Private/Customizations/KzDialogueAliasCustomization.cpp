// Copyright 2026 kirzo

#include "Customizations/KzDialogueAliasCustomization.h"
#include "Customizations/KzDialogueAliasLineRowCustomizer.h"
#include "Widgets/SKzPropertyStack.h"

#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueAliasCustomization"

TSharedRef<IPropertyTypeCustomization> FKzDialogueAliasCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueAliasCustomization>();
}

void FKzDialogueAliasCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	AliasNameHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueAlias, AliasName));
	SpeakerHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueAlias, Speaker));
	LineIdsHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueAlias, LineIds));

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
						const FKzDialogueAlias* Alias = reinterpret_cast<const FKzDialogueAlias*>(RawData);
						return Alias ? Alias->GetDisplayLabel() : FText::GetEmpty();
					})
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FKzDialogueAliasCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	if (AliasNameHandle.IsValid()) { StructBuilder.AddProperty(AliasNameHandle.ToSharedRef()); }
	if (SpeakerHandle.IsValid()) { StructBuilder.AddProperty(SpeakerHandle.ToSharedRef()); }

	// Custom-render LineIds as a SKzPropertyStack driven by the alias-aware row customizer.
	if (LineIdsHandle.IsValid())
	{
		LineRowCustomizer = MakeShared<FKzDialogueAliasLineRowCustomizer>();
		LineRowCustomizer->SetAliasHandle(StructHandle);

		TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
		LineRowCustomizer->SetResolveAssetFn([WeakUtilities]() -> UKzDialogueAsset*
			{
				TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin();
				if (!Utilities.IsValid()) { return nullptr; }

				const TArray<TWeakObjectPtr<UObject>>& Selected = Utilities->GetSelectedObjects();
				for (const TWeakObjectPtr<UObject>& WeakObj : Selected)
				{
					UObject* Obj = WeakObj.Get();
					if (!Obj) { continue; }

					if (UKzDialogueAsset* DirectCast = Cast<UKzDialogueAsset>(Obj))
					{
						return DirectCast;
					}
					if (UKzDialogueAsset* AsTyped = Obj->GetTypedOuter<UKzDialogueAsset>())
					{
						return AsTyped;
					}
				}
				return nullptr;
			});

		StructBuilder.AddCustomRow(LOCTEXT("LineIdsRowFilter", "Line Ids"))
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