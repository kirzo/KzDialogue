// Copyright 2026 kirzo

#include "Customizations/KzTokenTextCustomization.h"
#include "KzTokenText.h"

#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Widgets/SKzTokenTextEditor.h"

TSharedRef<IPropertyTypeCustomization> FKzTokenTextCustomization::MakeInstance()
{
	return MakeShared<FKzTokenTextCustomization>();
}

void FKzTokenTextCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	TSharedPtr<IPropertyHandle> TextHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzTokenText, Text));
	if (!TextHandle.IsValid()) { return; }

	// Any text edit rescans the cook references, so a token typed here packages its asset
	// even when this property is the only reference in the project.
	StructPropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([StructWeak = TWeakPtr<IPropertyHandle>(StructPropertyHandle)]()
	{
		if (TSharedPtr<IPropertyHandle> Struct = StructWeak.Pin())
		{
			Struct->EnumerateRawData([](void* RawData, const int32, const int32)
			{
				if (RawData) { static_cast<FKzTokenText*>(RawData)->RefreshTokenReferences(); }
				return true;
			});
		}
	}));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(325.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SKzTokenTextEditor, TextHandle.ToSharedRef())
		];
}