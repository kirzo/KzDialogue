// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;

/** Picks an FKzDialogueGraphEntryName from the entries of the selected node's graph, or of the graph its EntriesOf property names. */
class FKzDialogueGraphEntryNameCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:
	TArray<FName> GatherOptions() const;
	FName GetCurrentName() const;
	TSharedRef<SWidget> MakeMenu();
	void SetName(FName Name);

	TSharedPtr<IPropertyHandle> NameHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** The sibling graph property whose entries to list; none lists the selected node's own graph. */
	FName EntriesOf;
};