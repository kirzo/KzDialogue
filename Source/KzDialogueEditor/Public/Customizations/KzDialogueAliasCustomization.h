// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class FKzDialogueAliasLineRowCustomizer;

/**
 * Property customization for FKzDialogueAlias.
 *
 * Renders AliasName and Speaker as standard rows, then replaces the LineIds array
 * with a SKzPropertyStack driven by FKzDialogueAliasLineRowCustomizer (resolved
 * line labels + picker-based "Add" button filtered by the alias's speaker).
 */
class FKzDialogueAliasCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> AliasNameHandle;
	TSharedPtr<IPropertyHandle> SpeakerHandle;
	TSharedPtr<IPropertyHandle> LineIdsHandle;
	TWeakPtr<class IPropertyUtilities> PropertyUtilities;

	/** Row customizer for the LineIds array, lives for the lifetime of this customization. */
	TSharedPtr<class FKzDialogueLineFromAssetRowCustomizer> LineRowCustomizer;
};