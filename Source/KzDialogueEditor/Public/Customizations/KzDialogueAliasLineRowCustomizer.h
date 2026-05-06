// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/KzPropertyStackRowCustomizer.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Guid.h"

class UKzDialogueAsset;
class IPropertyHandle;
class IPropertyHandleArray;

/**
 * Row customizer for the FGuid entries inside FKzDialogueAlias::LineIds.
 *
 * Each FGuid in the array refers to a line that lives in the same dialogue asset
 * as the alias. This customizer:
 *   - Resolves each GUID against the owning asset and shows the line label.
 *   - Replaces the array's "Add" button with a picker that lists only the lines
 *     matching the alias's speaker and not yet used in the array.
 *
 * Asset is resolved lazily from the property handle's outer objects so this works
 * regardless of where the alias struct is being edited (asset editor, data table,
 * actor instance with a FKzDialogueAlias variable, etc.).
 */
class FKzDialogueAliasLineRowCustomizer : public FKzPropertyStackRowCustomizer
{
public:
	/**
	 * The handle for the FKzDialogueAlias struct that owns this LineIds array.
	 * Used to read the alias's Speaker for filtering, and to find the asset.
	 */
	void SetAliasHandle(TSharedPtr<IPropertyHandle> InAliasHandle);

	//~ Begin FKzPropertyStackRowCustomizer
	virtual TSharedRef<SWidget> BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual FText GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual FText GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual bool HasAddMenu() const override { return true; }
	virtual TSharedPtr<SWidget> BuildAddMenu(TSharedPtr<class IPropertyHandleArray> ArrayHandle);
	virtual bool TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const override;
	//~ End FKzPropertyStackRowCustomizer

	using FResolveAssetFn = TFunction<UKzDialogueAsset* ()>;
	void SetResolveAssetFn(FResolveAssetFn InFn) { ResolveAssetFn = MoveTemp(InFn); }

private:
	/**
	 * Resolve the dialogue asset that owns the alias being edited. Walks outer
	 * objects of the alias handle. Returns nullptr if not found.
	 */
	UKzDialogueAsset* ResolveAsset() const;

	/** Read the FGuid value stored in a row's handle. */
	FGuid ReadGuid(TSharedPtr<IPropertyHandle> Handle) const;

	/** Read the alias's speaker tag (returns empty if unavailable). */
	struct FGameplayTag GetAliasSpeakerTag() const;

	/** Picker callback: append the picked GUID to the LineIds array. */
	void OnLinePicked(struct FKzDialogueAssetReference InRef, float DefaultDuration,
		TWeakPtr<IPropertyHandleArray> WeakArrayHandle);

	/**
	 * Build the set of LineIds already present in the array, used by the picker
	 *  to hide them via its "Already used" filter.
	 */
	TSet<FGuid> CollectAlreadyUsed(TSharedPtr<IPropertyHandleArray> ArrayHandle) const;

	/** Weak handle to the FKzDialogueAlias struct owning the array. */
	TWeakPtr<IPropertyHandle> AliasHandle;

	FResolveAssetFn ResolveAssetFn;
};