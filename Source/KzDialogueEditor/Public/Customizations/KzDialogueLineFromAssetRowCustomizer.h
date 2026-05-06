// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/KzPropertyStackRowCustomizer.h"
#include "GameplayTagContainer.h"
#include "Misc/Guid.h"

class UKzDialogueAsset;
class IPropertyHandle;
class IPropertyHandleArray;

/**
 * Generic row customizer for arrays of FGuid that point to lines or aliases inside
 * a UKzDialogueAsset. Used by both:
 *   - FKzDialogueAlias::LineIds (asset resolved from the editor's selected objects;
 *     speaker filtered to match the alias's speaker; aliases themselves hidden).
 *   - FKzDialogueLineList::LineIds (asset resolved from the struct's Asset member;
 *     no speaker filter; aliases shown).
 *
 * Behavior is parameterized by callbacks set by the host customization. When a
 * callback isn't provided, sensible defaults apply (no speaker filter, aliases shown).
 */
class FKzDialogueLineFromAssetRowCustomizer : public FKzPropertyStackRowCustomizer
{
public:
	using FResolveAssetFn = TFunction<UKzDialogueAsset* ()>;
	using FResolveSpeakerFn = TFunction<FGameplayTag()>;

	void SetResolveAssetFn(FResolveAssetFn InFn) { ResolveAssetFn = MoveTemp(InFn); }
	void SetResolveSpeakerFn(FResolveSpeakerFn InFn) { ResolveSpeakerFn = MoveTemp(InFn); }

	/**
	 * Whether the picker should include aliases. Default: true.
	 * FKzDialogueAlias's LineIds set this to false (aliases can't reference aliases).
	 */
	void SetShowAliases(bool bInShow) { bShowAliases = bInShow; }

	//~ Begin FKzPropertyStackRowCustomizer
	virtual TSharedRef<SWidget> BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual FText GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual FText GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual bool HasAddMenu() const override { return true; }
	virtual TSharedPtr<SWidget> BuildAddMenu(TSharedPtr<IPropertyHandleArray> ArrayHandle) override;
	virtual bool TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const override;
	//~ End FKzPropertyStackRowCustomizer

private:
	UKzDialogueAsset* ResolveAsset() const { return ResolveAssetFn ? ResolveAssetFn() : nullptr; }
	FGameplayTag ResolveSpeaker() const { return ResolveSpeakerFn ? ResolveSpeakerFn() : FGameplayTag(); }

	FGuid ReadGuid(TSharedPtr<IPropertyHandle> Handle) const;

	void OnLinePicked(struct FKzDialogueAssetReference InRef, float DefaultDuration, TWeakPtr<IPropertyHandleArray> WeakArrayHandle);

	TSet<FGuid> CollectAlreadyUsed(TSharedPtr<IPropertyHandleArray> ArrayHandle) const;

	FResolveAssetFn   ResolveAssetFn;
	FResolveSpeakerFn ResolveSpeakerFn;
	bool bShowAliases = true;
};