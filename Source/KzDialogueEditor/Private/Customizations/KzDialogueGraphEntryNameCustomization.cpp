// Copyright 2026 kirzo

#include "Customizations/KzDialogueGraphEntryNameCustomization.h"
#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphNodeBodies.h"
#include "Graph/KzDialogueGraphNodes.h"
#include "KzEdGraphNode.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraph.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraphEntryNameCustomization"

TSharedRef<IPropertyTypeCustomization> FKzDialogueGraphEntryNameCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueGraphEntryNameCustomization>();
}

void FKzDialogueGraphEntryNameCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	NameHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueGraphEntryName, Name));
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();
	EntriesOf = FName(StructPropertyHandle->GetMetaData(TEXT("EntriesOf")));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(160.0f)
		[
			SNew(SComboButton)
				.OnGetMenuContent(this, &FKzDialogueGraphEntryNameCustomization::MakeMenu)
				.ContentPadding(FMargin(2.0f))
				.ButtonContent()
				[
					SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const FName Name = GetCurrentName();
							return Name.IsNone() ? LOCTEXT("NoEntry", "(no entry)") : FText::FromName(Name);
						})
						.Font(IDetailLayoutBuilder::GetDetailFont())
				]
		];
}

TArray<FName> FKzDialogueGraphEntryNameCustomization::GatherOptions() const
{
	if (!PropertyUtilities.IsValid())
	{
		return {};
	}

	for (const TWeakObjectPtr<UObject>& Object : PropertyUtilities->GetSelectedObjects())
	{
		const UKzEdGraphNode* Node = Cast<UKzEdGraphNode>(Object.Get());
		if (!Node || !Node->Node.IsValid())
		{
			continue;
		}

		if (EntriesOf.IsNone())
		{
			const UEdGraph* Graph = Node->GetGraph();
			return Graph ? KzDialogueGraphNodeBodies::GatherEntryNames(*Graph) : TArray<FName>();
		}

		// The graph is a property of the same node, read as the node holds it.
		const FObjectPropertyBase* GraphProperty = CastField<FObjectPropertyBase>(Node->Node.GetScriptStruct()->FindPropertyByName(EntriesOf));
		const UKzDialogueGraph* Called = GraphProperty ? Cast<UKzDialogueGraph>(GraphProperty->GetObjectPropertyValue_InContainer(Node->Node.GetMemory())) : nullptr;
		return Called ? KzDialogueGraphNodeBodies::GatherEntryNames(*Called) : TArray<FName>();
	}
	return {};
}

FName FKzDialogueGraphEntryNameCustomization::GetCurrentName() const
{
	FName Name;
	if (NameHandle.IsValid())
	{
		NameHandle->GetValue(Name);
	}
	return Name;
}

TSharedRef<SWidget> FKzDialogueGraphEntryNameCustomization::MakeMenu()
{
	FMenuBuilder Menu(true, nullptr);
	for (const FName Option : GatherOptions())
	{
		Menu.AddMenuEntry(FText::FromName(Option), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FKzDialogueGraphEntryNameCustomization::SetName, Option)));
	}
	return Menu.MakeWidget();
}

void FKzDialogueGraphEntryNameCustomization::SetName(FName Name)
{
	if (NameHandle.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("PickEntry", "Pick Entry"));
		NameHandle->SetValue(Name);
	}
}

#undef LOCTEXT_NAMESPACE