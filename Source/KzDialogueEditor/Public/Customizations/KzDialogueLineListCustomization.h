// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class FKzDialogueLineFromAssetRowCustomizer;

/**
 * Property customization for FKzDialogueLineList.
 *
 * Header: shows "(N lines)" or "(empty)".
 * Children:
 *   1. "Asset" — standard soft-object picker.
 *   2. "Lines" — SKzPropertyStack of FGuid entries, each resolved against the asset
 *      via FKzDialogueLineFromAssetRowCustomizer. The "Add" button opens the line
 *      picker scoped to the selected asset (no speaker filter, aliases shown).
 */
class FKzDialogueLineListCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> AssetHandle;
	TSharedPtr<IPropertyHandle> LineIdsHandle;

	TSharedPtr<FKzDialogueLineFromAssetRowCustomizer> LineRowCustomizer;
};