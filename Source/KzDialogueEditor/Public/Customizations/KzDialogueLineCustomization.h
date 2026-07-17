// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyUtilities;
class UKzDialogueAsset;

/**
 * Customizes the expanded FKzDialogueLine details. Adds a per-line notify timeline that
 * lives on the owning asset (keyed by line id) rather than on the line struct: lines are
 * edited as external structures, where an instanced subobject can't be created. The line's
 * runtime Timeline pointer is filled by the provider at resolve time. This is also the
 * future host for the visual timeline strip.
 */
class FKzDialogueLineCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	UKzDialogueAsset* ResolveOwningAsset() const;
	FGuid GetLineId() const;
	float GetDisplayDuration() const;
	FReply OnCreateTimelineClicked();
	FReply OnDeleteTimelineClicked();

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};