// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class UKzDialogueAsset;
class IDetailChildrenBuilder;

/**
 * Property customization for FKzDialogueLineRef.
 *
 * Renders the struct as two rows in the details panel:
 *   1. "Asset" — a standard soft-object picker.
 *   2. "Line / Alias" — a custom dropdown that opens SKzDialogueLinePicker scoped to
 *      the currently selected asset. Shows the resolved label as button text.
 *
 * The header row shows a compact summary "(speaker) text..." so users can see at a
 * glance what the reference resolves to without expanding it.
 */
class FKzDialogueLineRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** Build the dropdown menu content (the picker scoped to the current asset). */
	TSharedRef<SWidget> BuildPickerMenu();

	/** Apply a picked entry to the LineId property handle. */
	void OnEntryPicked(struct FKzDialogueAssetReference InRef, float DefaultDuration);

	/** Whether the dropdown should be enabled (asset has a valid value). */
	bool IsDropdownEnabled() const;

	/** Compact label shown on the header row and on the line dropdown button. */
	FText GetCurrentLineLabel() const;

	/**
	 * Resolve the current asset from its property handle. Returns nullptr if unset
	 * or unloaded; the dropdown loads it lazily.
	 */
	UKzDialogueAsset* GetCurrentAsset() const;

	/** Read the GUID currently stored in the LineId property handle. */
	FGuid GetCurrentGuid() const;

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> AssetHandle;
	TSharedPtr<IPropertyHandle> LineIdHandle;
};