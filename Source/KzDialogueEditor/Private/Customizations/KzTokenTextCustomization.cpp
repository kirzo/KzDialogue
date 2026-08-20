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