// Copyright 2026 kirzo

#include "Customizations/KzDialogueAliasCustomization.h"
#include "Customizations/KzDialogueLineFromAssetRowCustomizer.h"
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

	if (LineIdsHandle.IsValid())
	{
		LineRowCustomizer = MakeShared<FKzDialogueLineFromAssetRowCustomizer>();

		// Resolve asset from the detail view's selected objects (the host UObject's
		// outer chain leads to UKzDialogueAsset). Property handle outers don't work
		// for structs injected as external structures.
		TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
		LineRowCustomizer->SetResolveAssetFn([WeakUtilities]() -> UKzDialogueAsset*
			{
				TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin();
				if (!Utilities.IsValid()) { return nullptr; }
				for (const TWeakObjectPtr<UObject>& WeakObj : Utilities->GetSelectedObjects())
				{
					if (UObject* Obj = WeakObj.Get())
					{
						if (UKzDialogueAsset* DirectCast = Cast<UKzDialogueAsset>(Obj)) { return DirectCast; }
						if (UKzDialogueAsset* AsTyped = Obj->GetTypedOuter<UKzDialogueAsset>()) { return AsTyped; }
					}
				}
				return nullptr;
			});

		// Resolve speaker by reading the alias struct directly. The picker uses this
		// to filter to lines that belong to the alias's speaker (or narration when empty).
		TWeakPtr<IPropertyHandle> WeakStructHandle = StructHandle;
		LineRowCustomizer->SetResolveSpeakerFn([WeakStructHandle]() -> FGameplayTag
			{
				TSharedPtr<IPropertyHandle> Pin = WeakStructHandle.Pin();
				if (!Pin.IsValid()) { return FGameplayTag(); }
				void* RawData = nullptr;
				if (Pin->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
				{
					return FGameplayTag();
				}
				const FKzDialogueAlias* Alias = reinterpret_cast<const FKzDialogueAlias*>(RawData);
				return Alias ? Alias->Speaker.SpeakerTag : FGameplayTag();
			});

		// Aliases can't reference other aliases — picker only shows lines.
		LineRowCustomizer->SetShowAliases(false);

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
							.ListPadding(FMargin(0.0f, 5.0f))
					]
			];
	}
}

#undef LOCTEXT_NAMESPACE