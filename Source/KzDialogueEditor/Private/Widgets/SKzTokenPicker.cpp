// Copyright 2026 kirzo

#include "Widgets/SKzTokenPicker.h"
#include "KzNamedAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SKzTokenPicker"

TArray<FString> SKzTokenPicker::RecentTokens;

void SKzTokenPicker::Construct(const FArguments& InArgs)
{
	bAutocompleteMode = InArgs._bAutocompleteMode;
	OnTokenChosen = InArgs._OnTokenChosen;

	BuildNodes();

	TreeView = SNew(STreeView<TSharedPtr<FKzTokenNode>>)
		.TreeItemsSource(&VisibleNodes)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SKzTokenPicker::MakeNodeRow)
		.OnGetChildren_Lambda([](TSharedPtr<FKzTokenNode> Node, TArray<TSharedPtr<FKzTokenNode>>& OutChildren)
		{
			OutChildren = Node->Children;
		})
		// Click commits even when the item was already selected (selection alone does not).
		.OnMouseButtonClick_Lambda([this](TSharedPtr<FKzTokenNode> Node) { Choose(Node); });

	RebuildVisible();

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	if (!bAutocompleteMode)
	{
		SearchBox = SNew(SSearchBox)
			.HintText(LOCTEXT("TokenSearchHint", "Search tokens..."))
			.OnTextChanged_Lambda([this](const FText& NewText) { SetFilter(NewText.ToString()); })
			.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType)
			{
				// Enter takes the top match, so search-and-enter needs no mouse at all.
				if (CommitType == ETextCommit::OnEnter) { AcceptSelection(); }
			})
			.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent)
			{
				// Down jumps into the list for full keyboard navigation.
				if (KeyEvent.GetKey() == EKeys::Down && TreeView.IsValid() && VisibleNodes.Num() > 0)
				{
					FSlateApplication::Get().SetKeyboardFocus(TreeView);
					TreeView->SetSelection(VisibleNodes[0]);
					return FReply::Handled();
				}
				return FReply::Unhandled();
			});

		Content->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SearchBox.ToSharedRef()
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
					.ToolTipText(LOCTEXT("TokenTypeFilterTip", "Show only tokens of one asset type."))
					.OnGetMenuContent_Lambda([this]()
					{
						// Close-self-only: picking a type must not dismiss the hosting popup.
						FMenuBuilder TypeMenu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bInCloseSelfOnly=*/true);
						TypeMenu.AddMenuEntry(LOCTEXT("AllTokenTypes", "All types"), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this]() { TypeFilter.Reset(); RebuildVisible(); })));
						for (const FString& TypeName : TypeNames)
						{
							TypeMenu.AddMenuEntry(FText::FromString(TypeName), FText::GetEmpty(), FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([this, TypeName]() { TypeFilter = TypeName; RebuildVisible(); })));
						}
						return TypeMenu.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock).Text_Lambda([this]() { return TypeFilter.IsEmpty() ? LOCTEXT("AllTokenTypes", "All types") : FText::FromString(TypeFilter); })
					]
			]
		];
	}

	Content->AddSlot().FillHeight(1.0f).Padding(4.0f, bAutocompleteMode ? 4.0f : 0.0f, 4.0f, 4.0f)
	[
		AllNodes.IsEmpty()
			? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NoNamedAssets", "No named assets: set Token on a Speaker or Word asset first")).ColorAndOpacity(FSlateColor::UseSubduedForeground()))
			: StaticCastSharedRef<SWidget>(TreeView.ToSharedRef())
	];

	ChildSlot
	[
		SNew(SBox)
			.WidthOverride(bAutocompleteMode ? 340.0f : 420.0f)
			.HeightOverride(bAutocompleteMode ? 220.0f : 340.0f)
			[
				Content
			]
	];
}

void SKzTokenPicker::BuildNodes()
{
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> NamedAssets;
	Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);

	TSet<FName> Seen;
	for (const FAssetData& Data : NamedAssets)
	{
		FName Token;
		if (!Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), Token) || Token.IsNone() || Seen.Contains(Token)) { continue; }
		Seen.Add(Token);

		const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Data.ToSoftObjectPath().TryLoad());
		if (!Named) { continue; }

		TSharedPtr<FKzTokenNode> Base = MakeShared<FKzTokenNode>();
		Base->TokenText = FString::Printf(TEXT("{%s}"), *Token.ToString());
		Base->Preview = Named->ResolveName();
		Base->TypeName = Named->GetClass()->GetDisplayNameText().ToString();
		Base->AssetPath = Data.GetObjectPathString();
		Base->bEmptyPreview = Base->Preview.IsEmpty();
		TypeNames.AddUnique(Base->TypeName);

		for (const FName Part : Named->GetNameParts())
		{
			TSharedPtr<FKzTokenNode> Child = MakeShared<FKzTokenNode>();
			Child->TokenText = FString::Printf(TEXT("{%s:%s}"), *Token.ToString(), *Part.ToString());
			Child->Preview = Named->ResolveName(Part);
			Child->Description = Named->GetNamePartDescription(Part);
			Child->TypeName = Base->TypeName;
			Child->AssetPath = Base->AssetPath;
			Child->bEmptyPreview = Child->Preview.IsEmpty();
			Base->Children.Add(Child);
		}

		AllNodes.Add(Base);
	}

	AllNodes.Sort([](const TSharedPtr<FKzTokenNode>& A, const TSharedPtr<FKzTokenNode>& B) { return A->TokenText < B->TokenText; });
	TypeNames.Sort();
}

void SKzTokenPicker::RebuildVisible()
{
	VisibleNodes.Reset();

	auto PassesType = [this](const TSharedPtr<FKzTokenNode>& Node) { return TypeFilter.IsEmpty() || Node->TypeName == TypeFilter; };
	auto Matches = [this](const TSharedPtr<FKzTokenNode>& Node)
	{
		return Node->TokenText.Contains(Filter, ESearchCase::IgnoreCase) || Node->Preview.ToString().Contains(Filter, ESearchCase::IgnoreCase);
	};

	if (Filter.IsEmpty())
	{
		for (const TSharedPtr<FKzTokenNode>& Base : AllNodes)
		{
			if (PassesType(Base)) { VisibleNodes.Add(Base); }
		}

		// Recently used tokens pull their base to the top - the REAL node, expander included,
		// so nothing appears twice. Ties keep the alphabetical order.
		auto RecentRank = [](const TSharedPtr<FKzTokenNode>& Base)
		{
			int32 Best = RecentTokens.IndexOfByKey(Base->TokenText);
			if (Best == INDEX_NONE) { Best = MAX_int32; }
			for (const TSharedPtr<FKzTokenNode>& Child : Base->Children)
			{
				const int32 ChildRank = RecentTokens.IndexOfByKey(Child->TokenText);
				if (ChildRank != INDEX_NONE && ChildRank < Best) { Best = ChildRank; }
			}
			return Best;
		};
		VisibleNodes.StableSort([&RecentRank](const TSharedPtr<FKzTokenNode>& A, const TSharedPtr<FKzTokenNode>& B) { return RecentRank(A) < RecentRank(B); });
	}
	else
	{
		// Searching flattens: matching bases and matching parts all become roots.
		for (const TSharedPtr<FKzTokenNode>& Base : AllNodes)
		{
			if (!PassesType(Base)) { continue; }
			if (Matches(Base))
			{
				TSharedPtr<FKzTokenNode> Flat = MakeShared<FKzTokenNode>(*Base);
				Flat->Children.Reset();
				VisibleNodes.Add(Flat);
			}
			for (const TSharedPtr<FKzTokenNode>& Child : Base->Children)
			{
				if (Matches(Child)) { VisibleNodes.Add(Child); }
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		if (bAutocompleteMode)
		{
			// Keyboard-driven: keep something selected so Enter always has a target.
			TreeView->ClearSelection();
			if (VisibleNodes.Num() > 0) { TreeView->SetSelection(VisibleNodes[0]); }
		}
	}
}

void SKzTokenPicker::SetFilter(const FString& InFilter)
{
	if (Filter != InFilter)
	{
		Filter = InFilter;
		RebuildVisible();
	}
}

void SKzTokenPicker::MoveSelection(int32 Delta)
{
	if (!TreeView.IsValid() || VisibleNodes.Num() == 0) { return; }

	// Navigate the rows as displayed: roots plus the children of expanded ones.
	TArray<TSharedPtr<FKzTokenNode>> Flat;
	for (const TSharedPtr<FKzTokenNode>& Root : VisibleNodes)
	{
		Flat.Add(Root);
		if (TreeView->IsItemExpanded(Root)) { Flat.Append(Root->Children); }
	}
	if (Flat.Num() == 0) { return; }

	const TArray<TSharedPtr<FKzTokenNode>> Selected = TreeView->GetSelectedItems();
	int32 Index = Selected.Num() > 0 ? Flat.IndexOfByKey(Selected[0]) : INDEX_NONE;
	Index = Index == INDEX_NONE ? 0 : (Index + Delta + Flat.Num()) % Flat.Num();
	TreeView->SetSelection(Flat[Index]);
	TreeView->RequestScrollIntoView(Flat[Index]);
}

void SKzTokenPicker::SetSelectionExpanded(bool bExpand)
{
	if (!TreeView.IsValid()) { return; }

	const TArray<TSharedPtr<FKzTokenNode>> Selected = TreeView->GetSelectedItems();
	if (Selected.Num() == 0) { return; }
	const TSharedPtr<FKzTokenNode> Node = Selected[0];

	if (VisibleNodes.Contains(Node))
	{
		if (!bExpand || Node->Children.Num() > 0)
		{
			TreeView->SetItemExpansion(Node, bExpand);
		}
	}
	else if (!bExpand)
	{
		// A part: collapsing jumps back to its base.
		for (const TSharedPtr<FKzTokenNode>& Root : VisibleNodes)
		{
			if (Root->Children.Contains(Node))
			{
				TreeView->SetItemExpansion(Root, false);
				TreeView->SetSelection(Root);
				TreeView->RequestScrollIntoView(Root);
				break;
			}
		}
	}
}

bool SKzTokenPicker::AcceptSelection()
{
	if (!TreeView.IsValid() || VisibleNodes.Num() == 0) { return false; }

	const TArray<TSharedPtr<FKzTokenNode>> Selected = TreeView->GetSelectedItems();
	Choose(Selected.Num() > 0 ? Selected[0] : VisibleNodes[0]);
	return true;
}

TSharedPtr<SWidget> SKzTokenPicker::GetWidgetToFocus() const
{
	return SearchBox;
}

void SKzTokenPicker::Choose(const TSharedPtr<FKzTokenNode>& Node)
{
	if (!Node.IsValid()) { return; }

	RecentTokens.Remove(Node->TokenText);
	RecentTokens.Insert(Node->TokenText, 0);
	if (RecentTokens.Num() > 8) { RecentTokens.SetNum(8); }

	OnTokenChosen.ExecuteIfBound(Node->TokenText);
}

TSharedRef<ITableRow> SKzTokenPicker::MakeNodeRow(TSharedPtr<FKzTokenNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText Tip = FText::Format(LOCTEXT("TokenRowTip", "Resolves to: {0}\n{1}\n{2}"), Node->Preview, FText::FromString(Node->TypeName), FText::FromString(Node->AssetPath));
	if (!Node->Description.IsEmpty())
	{
		Tip = FText::Format(LOCTEXT("TokenRowTipWithDesc", "{0}\n\n{1}"), Node->Description, Tip);
	}

	// Empty-resolving rows stay pickable but read as "will render nothing" at a glance.
	const FText PreviewText = Node->bEmptyPreview ? LOCTEXT("EmptyPreview", "(empty)") : Node->Preview;

	return SNew(STableRow<TSharedPtr<FKzTokenNode>>, OwnerTable)
		.ToolTipText(Tip)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Node->TokenText))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.ColorAndOpacity(Node->bEmptyPreview ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 2.0f, 4.0f, 2.0f)
			[
				SNew(STextBlock)
					.Text(PreviewText)
					.Justification(ETextJustify::Right)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

#undef LOCTEXT_NAMESPACE