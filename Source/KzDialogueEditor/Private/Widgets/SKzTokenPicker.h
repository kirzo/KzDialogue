// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SSearchBox;
class STableViewBase;
template <typename ItemType> class STreeView;

DECLARE_DELEGATE_OneParam(FOnKzTokenChosen, const FString& /*TokenText*/);

/**
 * Named-asset token browser shared by the line editor's "{}" button and the inline "{"
 * autocomplete. A tree of base tokens ("{Kirzo}") expanding into their parts
 * ("{Kirzo:given}"), each with a live resolution preview; searching flattens and filters
 * across both levels, matching token text and preview. Empty-resolving rows are dimmed
 * and annotated. Recently inserted tokens surface on top of the unfiltered list.
 *
 * Button mode shows the search box and a type filter. Autocomplete mode hides both: the
 * host feeds SetFilter from the typed fragment and drives the selection with
 * MoveSelection / AcceptSelection while its own text box keeps the keyboard focus.
 */
class SKzTokenPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzTokenPicker)
		: _bAutocompleteMode(false)
		, _bBaseTokensOnly(false)
		{}
		/** Fired with the full insertable token text ("{Kirzo:given}"). */
		SLATE_EVENT(FOnKzTokenChosen, OnTokenChosen)
		/** Compact, externally driven variant for the inline "{" completion. */
		SLATE_ARGUMENT(bool, bAutocompleteMode)
		/** Offers base tokens only, no ":part" children: for hosts whose slot is a bare token (a Token pin). */
		SLATE_ARGUMENT(bool, bBaseTokensOnly)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** External filter over token text and preview (autocomplete mode; the search box uses it too). */
	void SetFilter(const FString& InFilter);

	/** Moves the keyboard-driven selection by Delta rows over the visible rows (expanded children included), wrapping at the ends. */
	void MoveSelection(int32 Delta);

	/** Expands the selected base token (revealing its parts) or collapses it; collapsing a selected part jumps back to its base. Bound to Left/Right in the autocomplete session. */
	void SetSelectionExpanded(bool bExpand);

	/** Chooses the selected row, or the top one when nothing is selected. False when the list is empty. */
	bool AcceptSelection();

	/** The widget the hosting menu should focus on open (the search box; null in autocomplete mode). */
	TSharedPtr<SWidget> GetWidgetToFocus() const;

private:
	/** One pickable row: a base token or one of its parts. */
	struct FKzTokenNode
	{
		FString TokenText;
		FText Preview;
		FText Description;
		FString TypeName;
		FString AssetPath;
		bool bEmptyPreview = false;
		TArray<TSharedPtr<FKzTokenNode>> Children;
	};

	/** Every base token with its part children, built once from the asset registry. */
	TArray<TSharedPtr<FKzTokenNode>> AllNodes;
	/** Distinct named-asset type display names, for the type filter. */
	TArray<FString> TypeNames;

	/** Roots currently shown: bases (plus recents) unfiltered, flat matches while searching. */
	TArray<TSharedPtr<FKzTokenNode>> VisibleNodes;

	FString Filter;
	FString TypeFilter;
	bool bAutocompleteMode = false;
	bool bBaseTokensOnly = false;
	FOnKzTokenChosen OnTokenChosen;

	TSharedPtr<STreeView<TSharedPtr<FKzTokenNode>>> TreeView;
	TSharedPtr<SSearchBox> SearchBox;

	void BuildNodes();
	void RebuildVisible();
	void Choose(const TSharedPtr<FKzTokenNode>& Node);
	TSharedRef<ITableRow> MakeNodeRow(TSharedPtr<FKzTokenNode> Node, const TSharedRef<STableViewBase>& OwnerTable);

	/** Session-wide memory of the last inserted tokens, most recent first. */
	static TArray<FString> RecentTokens;
};