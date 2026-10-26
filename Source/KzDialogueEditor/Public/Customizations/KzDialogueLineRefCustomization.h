// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class IPropertyUtilities;
class UKzDialogueAsset;
class IDetailChildrenBuilder;

/**
 * Property customization for FKzDialogueLineRef.
 *
 * Renders the struct as two rows in the details panel:
 *   1. "Asset": a standard soft-object picker.
 *   2. "Line / Alias": a custom dropdown that opens SKzDialogueLinePicker scoped to
 *      the currently selected asset.
 *
 * The header row shows a compact summary "(speaker) text..." so users can see at a
 * glance what the reference resolves to without expanding it.
 *
 * Inside a dialogue graph an unset asset falls back to the graph's lines asset. With the
 * InlineLine metadata the referenced line is edited in place, on its asset, and a button
 * adds a new line to pick.
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

	/** Adds an empty line to the current asset and points the ref at it. */
	FReply OnNewLineClicked();

	/** Points the ref at Id in Asset, storing the asset when the ref only fell back to it. */
	void SetEntry(UKzDialogueAsset& Asset, const FGuid& Id);

	/** After an asset change: clears the id when the new asset has no such entry, as the K2 nodes do. */
	void ClearStaleLineId();

	/** Whether the dropdown should be enabled (asset has a valid value). */
	bool IsDropdownEnabled() const;

	/** Compact label shown on the header row and on the line dropdown button. */
	FText GetCurrentLineLabel() const;

	/** The ref's asset, or the dialogue graph's lines asset when the ref has none. */
	UKzDialogueAsset* GetCurrentAsset() const;

	/**
	 * Resolve the ref's own asset from its property handle. Returns nullptr if unset;
	 * an unloaded asset is loaded lazily.
	 */
	UKzDialogueAsset* GetRefAsset() const;

	/** The lines asset of the dialogue graph whose node holds the ref; null elsewhere. */
	UKzDialogueAsset* GetGraphLinesAsset() const;

	/** Read the GUID currently stored in the LineId property handle. */
	FGuid GetCurrentGuid() const;

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> AssetHandle;
	TSharedPtr<IPropertyHandle> LineIdHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
	bool bInlineLine = false;
};