// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;

/** Picks an FKzDialogueGraphTarget from the nodes and Choice options of the graph whose node is selected. */
class FKzDialogueGraphTargetCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:
	struct FEntry
	{
		FGuid Id;
		FText Label;
	};

	TArray<FEntry> GatherEntries() const;
	FGuid GetCurrentId() const;
	FText GetCurrentLabel() const;
	TSharedRef<SWidget> MakeMenu();
	void SetId(FGuid Id);

	TSharedPtr<IPropertyHandle> IdHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
	mutable FGuid LabelId;
	mutable FText Label;
};