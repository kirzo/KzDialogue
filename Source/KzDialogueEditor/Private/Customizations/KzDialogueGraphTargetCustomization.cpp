// Copyright 2026 kirzo

#include "Customizations/KzDialogueGraphTargetCustomization.h"
#include "Graph/KzDialogueGraphConditions.h"
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

#define LOCTEXT_NAMESPACE "KzDialogueGraphTargetCustomization"

TSharedRef<IPropertyTypeCustomization> FKzDialogueGraphTargetCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueGraphTargetCustomization>();
}

void FKzDialogueGraphTargetCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	IdHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueGraphTarget, Id));
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.0f)
		[
			SNew(SComboButton)
				.OnGetMenuContent(this, &FKzDialogueGraphTargetCustomization::MakeMenu)
				.ContentPadding(FMargin(2.0f))
				.ButtonContent()
				[
					SNew(STextBlock)
						.Text_Lambda([this]() { return GetCurrentLabel(); })
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
		];
}

TArray<FKzDialogueGraphTargetCustomization::FEntry> FKzDialogueGraphTargetCustomization::GatherEntries() const
{
	TArray<FEntry> Entries;
	const UEdGraph* Graph = nullptr;
	if (PropertyUtilities.IsValid())
	{
		for (const TWeakObjectPtr<UObject>& Object : PropertyUtilities->GetSelectedObjects())
		{
			if (const UEdGraphNode* Selected = Cast<UEdGraphNode>(Object.Get()))
			{
				Graph = Selected->GetGraph();
				break;
			}
		}
	}
	if (!Graph)
	{
		return Entries;
	}

	for (const UEdGraphNode* Each : Graph->Nodes)
	{
		const UKzEdGraphNode* Node = Cast<UKzEdGraphNode>(Each);
		if (!Node || !Node->Node.IsValid())
		{
			continue;
		}

		// One line per entry: the title, then what tells this node from its siblings.
		const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(": "));
		TArray<FText> Texts;
		Node->Node.Get().GetSearchTexts(Texts);

		if (const FKzDialogueGraphNode_Choice* Choice = Node->Node.GetPtr<FKzDialogueGraphNode_Choice>())
		{
			Entries.Add({ Node->NodeGuid, FText::FromString(Title) });
			for (int32 Index = 0; Index < Choice->Options.Num(); ++Index)
			{
				const FText Text = Texts.IsValidIndex(Index) ? Texts[Index] : FText::GetEmpty();
				Entries.Add({ Choice->Options[Index].Id, FText::Format(LOCTEXT("OptionEntry", "{0}, option {1}: {2}"), FText::FromString(Title), Index + 1, Text) });
			}
		}
		else if (Texts.Num() > 0 && !Texts[0].IsEmpty())
		{
			Entries.Add({ Node->NodeGuid, FText::Format(INVTEXT("{0}: {1}"), FText::FromString(Title), Texts[0]) });
		}
		else
		{
			Entries.Add({ Node->NodeGuid, FText::FromString(Title) });
		}
	}
	return Entries;
}

FGuid FKzDialogueGraphTargetCustomization::GetCurrentId() const
{
	FGuid Id;
	FString IdString;
	if (IdHandle.IsValid() && IdHandle->GetValueAsFormattedString(IdString) == FPropertyAccess::Success)
	{
		FGuid::Parse(IdString, Id);
	}
	return Id;
}

FText FKzDialogueGraphTargetCustomization::GetCurrentLabel() const
{
	// Gathering resolves every line of the graph: done again only when the target changes.
	const FGuid Id = GetCurrentId();
	if (Id == LabelId && !Label.IsEmpty())
	{
		return Label;
	}

	LabelId = Id;
	Label = LOCTEXT("ThisNode", "(this node)");
	if (Id.IsValid())
	{
		const TArray<FEntry> Entries = GatherEntries();
		const FEntry* Found = Entries.FindByPredicate([&Id](const FEntry& Entry) { return Entry.Id == Id; });
		Label = Found ? Found->Label : LOCTEXT("Missing", "(missing node)");
	}
	return Label;
}

TSharedRef<SWidget> FKzDialogueGraphTargetCustomization::MakeMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("ThisNode", "(this node)"), LOCTEXT("ThisNodeTip", "The node holding the condition."), FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FKzDialogueGraphTargetCustomization::SetId, FGuid())));
	Menu.AddSeparator();
	for (const FEntry& Entry : GatherEntries())
	{
		Menu.AddMenuEntry(Entry.Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FKzDialogueGraphTargetCustomization::SetId, Entry.Id)));
	}
	return Menu.MakeWidget();
}

void FKzDialogueGraphTargetCustomization::SetId(FGuid Id)
{
	if (IdHandle.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("PickTarget", "Pick dialogue graph target"));
		IdHandle->SetValueFromFormattedString(Id.ToString(EGuidFormats::Digits));
	}
}

#undef LOCTEXT_NAMESPACE