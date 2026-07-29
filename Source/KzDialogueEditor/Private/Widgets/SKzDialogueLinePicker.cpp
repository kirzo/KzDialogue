// Copyright 2026 kirzo

#include "Widgets/SKzDialogueLinePicker.h"
#include "KzDialogueEditorStyle.h"
#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "Sound/SoundBase.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "KzDialogueLinePicker"

void SKzDialogueLinePicker::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	OnEntryPicked = InArgs._OnEntryPicked;
	AlreadyUsedLineIds = InArgs._AlreadyUsedLineIds;
	RequiredSpeaker = InArgs._RequiredSpeaker;
	bRequireExactSpeakerMatch = InArgs._bRequireExactSpeakerMatch;
	bShowAliases = InArgs._bShowAliases;
	bHideAlreadyUsed = AlreadyUsedLineIds.Num() > 0;

	BuildAllItems();
	GatherDistinctSpeakers();
	GatherDistinctLineTags();
	RebuildVisibleItems();

	ChildSlot
		[
			SNew(SBox)
				.WidthOverride(320.f)
				.MinDesiredHeight(80.f)
				.MaxDesiredHeight(420.f)
				[
					// Outer panel
					SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("Menu.Background"))
						.Padding(FMargin(6.f))
						[
							SNew(SVerticalBox)

								// Filter button + search box, fused into a single bar.
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)

										+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
										[
											SAssignNew(FilterButton, SComboButton)
												.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
												.ForegroundColor(FSlateColor::UseStyle())
												.HasDownArrow(true)
												.ContentPadding(FMargin(4.f, 2.f))
												.OnGetMenuContent(this, &SKzDialogueLinePicker::BuildFilterMenu)
												.ToolTipText_Lambda([this]()
													{
														const int32 N = GetActiveFilterCount();
														return N > 0
															? FText::Format(LOCTEXT("FilterTipActive", "Filters ({0} active)"), FText::AsNumber(N))
															: LOCTEXT("FilterTip", "Filter the line list");
													})
												.ButtonContent()
												[
													SNew(SImage)
														.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
														.ColorAndOpacity_Lambda([this]()
															{
																return GetActiveFilterCount() > 0
																	? FSlateColor(FLinearColor(0.20f, 0.55f, 1.00f))
																	: FSlateColor::UseForeground();
															})
												]
										]

									+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
										[
											SAssignNew(SearchBox, SSearchBox)
												.HintText(LOCTEXT("SearchHint", "Search lines..."))
												.OnTextChanged(this, &SKzDialogueLinePicker::OnSearchTextChanged)
												.OnTextCommitted(this, &SKzDialogueLinePicker::OnSearchTextCommitted)
										]
								]

							// The list itself.
							+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 4, 0, 0)
								[
									SAssignNew(ListView, SListView<KzDialoguePickerInternal::FEntryPtr>)
										.ListItemsSource(&VisibleItems)
										.OnGenerateRow(this, &SKzDialogueLinePicker::OnGenerateRow)
										.OnMouseButtonClick(this, &SKzDialogueLinePicker::OnItemClicked)
										.SelectionMode(ESelectionMode::Single)
								]
						]
				]
		];

	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SKzDialogueLinePicker::FocusSearchBoxOnce));
}

// =======================================================================================
// Building items / filter sources
// =======================================================================================

void SKzDialogueLinePicker::BuildAllItems()
{
	AllItems.Reset();

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!IsValid(AssetPtr)) { return; }

	// Speaker part of the search haystack: asset name + resolved display name.
	auto SpeakerHaystack = [](const UKzSpeakerAsset* Speaker)
	{
		return Speaker ? Speaker->GetName() + TEXT(" ") + Speaker->GetResolvedDisplayName().ToString() : FString();
	};

	// Lines
	for (const FKzDialogueLine& Line : AssetPtr->Lines)
	{
		auto Entry = MakeShared<KzDialoguePickerInternal::FEntry>();
		Entry->bIsAlias = false;
		Entry->Id = Line.LineId;
		Entry->DisplayText = Line.GetDisplayLabel().ToString();
		Entry->Speaker = Line.Speaker.Asset;
		Entry->LineTags = Line.Tags;
		Entry->bHasAudio = !Line.Audio.IsNull();
		Entry->DefaultDuration = Line.Duration > 0.f ? Line.Duration : 2.f;

		// Haystack: speaker + text, lowercase.
		Entry->SearchHaystack = (SpeakerHaystack(Line.Speaker.Asset) + TEXT(" ") + Entry->DisplayText).ToLower();
		AllItems.Add(Entry);
	}

	// Aliases
	for (const FKzDialogueAlias& Alias : AssetPtr->Aliases)
	{
		auto Entry = MakeShared<KzDialoguePickerInternal::FEntry>();
		Entry->bIsAlias = true;
		Entry->Id = Alias.AliasId;
		Entry->DisplayText = Alias.GetDisplayLabel().ToString();
		Entry->Speaker = Alias.Speaker.Asset;
		Entry->LineTags = FGameplayTagContainer();
		Entry->bHasAudio = false;
		Entry->DefaultDuration = 2.f;

		Entry->SearchHaystack = (SpeakerHaystack(Alias.Speaker.Asset) + TEXT(" ") + Alias.AliasName.ToString()).ToLower();
		AllItems.Add(Entry);
	}
}

void SKzDialogueLinePicker::GatherDistinctSpeakers()
{
	DistinctSpeakers.Reset();

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!IsValid(AssetPtr)) { return; }

	// Collect distinct speakers, preserving insertion order for a stable filter UI.
	// A null entry stands for narration lines.
	TSet<TWeakObjectPtr<const UKzSpeakerAsset>> Seen;
	for (const FKzDialogueLine& Line : AssetPtr->Lines)
	{
		const TWeakObjectPtr<const UKzSpeakerAsset> Key = Line.Speaker.Asset;
		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			DistinctSpeakers.Add(Key);
		}
	}
}

void SKzDialogueLinePicker::GatherDistinctLineTags()
{
	DistinctLineTags.Reset();

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!IsValid(AssetPtr)) { return; }

	TSet<FGameplayTag> Seen;
	for (const FKzDialogueLine& Line : AssetPtr->Lines)
	{
		for (auto It = Line.Tags.CreateConstIterator(); It; ++It)
		{
			const FGameplayTag& LineTag = *It;
			if (LineTag.IsValid() && !Seen.Contains(LineTag))
			{
				Seen.Add(LineTag);
				DistinctLineTags.Add(LineTag);
			}
		}
	}

	// Alphabetical sort so the filter UI is stable regardless of authoring order.
	DistinctLineTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
}

// =======================================================================================
// Filtering
// =======================================================================================

void SKzDialogueLinePicker::RebuildVisibleItems()
{
	VisibleItems.Reset();
	VisibleItems.Reserve(AllItems.Num() + 2); // headers add at most 2 entries

	// First pass: bucket each item into lines / aliases, applying all filters.
	TArray<KzDialoguePickerInternal::FEntryPtr> VisibleLines;
	TArray<KzDialoguePickerInternal::FEntryPtr> VisibleAliases;

	for (const KzDialoguePickerInternal::FEntryPtr& Item : AllItems)
	{
		if (!Item.IsValid()) { continue; }

		// Hard speaker constraint imposed by the caller.
		if (RequiredSpeaker.IsValid())
		{
			if (Item->Speaker != RequiredSpeaker) { continue; }
		}
		else if (bRequireExactSpeakerMatch)
		{
			if (Item->Speaker.IsValid()) { continue; }
		}

		// Aliases visibility (e.g. Sequencer track disables them).
		if (Item->bIsAlias && !bShowAliases) { continue; }

		// Search text.
		if (!CurrentSearchText.IsEmpty() && !Item->SearchHaystack.Contains(CurrentSearchText))
		{
			continue;
		}

		// Speaker filter. A null weak pointer is the narration key.
		if (SelectedSpeakerFilters.Num() > 0)
		{
			if (!SelectedSpeakerFilters.Contains(Item->Speaker)) { continue; }
		}

		// Line tag filter.
		if (SelectedLineTagFilters.Num() > 0)
		{
			bool bMatch = false;
			for (const FGameplayTag& Wanted : SelectedLineTagFilters)
			{
				if (Item->LineTags.HasTagExact(Wanted)) { bMatch = true; break; }
			}
			if (!bMatch) { continue; }
		}

		// Hide already-used.
		if (bHideAlreadyUsed && AlreadyUsedLineIds.Contains(Item->Id))
		{
			continue;
		}

		if (Item->bIsAlias) { VisibleAliases.Add(Item); }
		else { VisibleLines.Add(Item); }
	}

	// Second pass: assemble final list with headers between sections. Headers only
	// appear when both sections have content — avoids a lone header at the top of
	// a list with only one kind of entry.
	auto MakeHeader = [](const FString& Label) -> KzDialoguePickerInternal::FEntryPtr
		{
			auto Header = MakeShared<KzDialoguePickerInternal::FEntry>();
			Header->bIsHeader = true;
			Header->DisplayText = Label;
			return Header;
		};

	const bool bHaveBoth = VisibleAliases.Num() > 0 && VisibleLines.Num() > 0;

	if (VisibleAliases.Num() > 0)
	{
		if (bHaveBoth) { VisibleItems.Add(MakeHeader(TEXT("Aliases"))); }
		VisibleItems.Append(VisibleAliases);
	}

	if (VisibleLines.Num() > 0)
	{
		if (bHaveBoth) { VisibleItems.Add(MakeHeader(TEXT("Lines"))); }
		VisibleItems.Append(VisibleLines);
	}

	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
}

void SKzDialogueLinePicker::OnSearchTextChanged(const FText& NewText)
{
	CurrentSearchText = NewText.ToString().TrimStartAndEnd().ToLower();
	RebuildVisibleItems();
}

void SKzDialogueLinePicker::OnSearchTextCommitted(const FText& /*Text*/, ETextCommit::Type Type)
{
	if (Type == ETextCommit::OnEnter && VisibleItems.Num() > 0)
	{
		OnItemClicked(VisibleItems[0]);
	}
}

// =======================================================================================
// Filter menu UI
// =======================================================================================

TSharedRef<SWidget> SKzDialogueLinePicker::BuildFilterMenu()
{
	// Keep the menu open while the user toggles filters; they can dismiss it by
	// clicking outside. Matches Content Browser's filter dropdown behaviour.
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	// ---- Visibility ---------------------------------------------------------------
	if (AlreadyUsedLineIds.Num() > 0)
	{
		MenuBuilder.BeginSection("KzDialoguePicker_Visibility", LOCTEXT("VisibilitySection", "Visibility"));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideUsed", "Hide Already Used"),
			LOCTEXT("HideUsedTip", "Hide lines already used as sections in this track"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Level.VisibleIcon16x"),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
					{
						bHideAlreadyUsed = !bHideAlreadyUsed;
						RebuildVisibleItems();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bHideAlreadyUsed; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
		MenuBuilder.EndSection();
	}

	// ---- Speakers -----------------------------------------------------------------
	if (DistinctSpeakers.Num() > 1)
	{
		MenuBuilder.BeginSection("KzDialoguePicker_Speakers", LOCTEXT("SpeakersSection", "Speakers"));
		for (const TWeakObjectPtr<const UKzSpeakerAsset>& Speaker : DistinctSpeakers)
		{
			const FText Label = GetSpeakerDisplayLabel(Speaker);
			MenuBuilder.AddMenuEntry(
				Label,
				FText::Format(LOCTEXT("FilterBySpeakerTip", "Show only lines spoken by {0}"), Label),
				FSlateIcon(FKzDialogueEditorStyle::Get().GetStyleSetName(), "Kz.Dialogue.Icon"),
				FUIAction(
					FExecuteAction::CreateSP(this, &SKzDialogueLinePicker::ToggleSpeakerFilter, Speaker),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([this, Speaker]()
						{
							return SelectedSpeakerFilters.Contains(Speaker);
						})),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
		}
		MenuBuilder.EndSection();
	}

	// ---- Tags ---------------------------------------------------------------------
	if (DistinctLineTags.Num() > 0)
	{
		MenuBuilder.BeginSection("KzDialoguePicker_Tags", LOCTEXT("TagsSection", "Tags"));
		for (const FGameplayTag& LineTag : DistinctLineTags)
		{
			const FText Label = FText::FromString(LineTag.ToString());
			MenuBuilder.AddMenuEntry(
				Label,
				FText::Format(LOCTEXT("FilterByTagTip", "Show only lines tagged with {0}"), Label),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Tag"),
				FUIAction(
					FExecuteAction::CreateSP(this, &SKzDialogueLinePicker::ToggleLineTagFilter, LineTag),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([this, LineTag]()
						{
							return SelectedLineTagFilters.Contains(LineTag);
						})),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
		}
		MenuBuilder.EndSection();
	}

	// ---- Clear all (only if anything could be cleared) ----------------------------
	const bool bHasAnyFilterableMetadata =
		DistinctSpeakers.Num() > 1 ||
		DistinctLineTags.Num() > 0 ||
		AlreadyUsedLineIds.Num() > 0;

	if (bHasAnyFilterableMetadata)
	{
		MenuBuilder.BeginSection("KzDialoguePicker_Actions", LOCTEXT("ActionsSection", "Actions"));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ClearAll", "Clear All Filters"),
			LOCTEXT("ClearAllTip", "Reset all active filters"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SKzDialogueLinePicker::ClearAllFilters),
				FCanExecuteAction::CreateLambda([this]() { return GetActiveFilterCount() > 0; })));
		MenuBuilder.EndSection();
	}
	else
	{
		// Friendly empty state.
		MenuBuilder.AddWidget(
			SNew(SBox).Padding(FMargin(8.f))
			[
				SNew(STextBlock)
					.Text(LOCTEXT("NoFilters", "No filterable metadata in this asset."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			],
			FText::GetEmpty(), /*bNoIndent=*/true);
	}

	return MenuBuilder.MakeWidget();
}

void SKzDialogueLinePicker::ToggleSpeakerFilter(TWeakObjectPtr<const UKzSpeakerAsset> Speaker)
{
	if (SelectedSpeakerFilters.Contains(Speaker)) { SelectedSpeakerFilters.Remove(Speaker); }
	else { SelectedSpeakerFilters.Add(Speaker); }
	RebuildVisibleItems();
}

void SKzDialogueLinePicker::ToggleLineTagFilter(FGameplayTag LineTag)
{
	if (SelectedLineTagFilters.Contains(LineTag)) { SelectedLineTagFilters.Remove(LineTag); }
	else { SelectedLineTagFilters.Add(LineTag); }
	RebuildVisibleItems();
}

void SKzDialogueLinePicker::ClearAllFilters()
{
	SelectedSpeakerFilters.Reset();
	SelectedLineTagFilters.Reset();
	bHideAlreadyUsed = false;
	RebuildVisibleItems();
}

int32 SKzDialogueLinePicker::GetActiveFilterCount() const
{
	int32 Count = 0;
	Count += SelectedSpeakerFilters.Num();
	Count += SelectedLineTagFilters.Num();
	if (bHideAlreadyUsed) { ++Count; }
	return Count;
}

FText SKzDialogueLinePicker::GetSpeakerDisplayLabel(TWeakObjectPtr<const UKzSpeakerAsset> Speaker) const
{
	const UKzSpeakerAsset* Ptr = Speaker.Get();
	if (!Ptr)
	{
		return NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
	}

	// Surface both when they differ: the resolved name for reading plus the asset name to
	// disambiguate two characters sharing a display name.
	const FString AssetName = Ptr->GetName();
	const FString Resolved = Ptr->GetResolvedDisplayName().ToString();
	if (!Resolved.IsEmpty() && Resolved != AssetName)
	{
		return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *Resolved, *AssetName));
	}

	return FText::FromString(AssetName);
}

// =======================================================================================
// Other event handlers (unchanged)
// =======================================================================================

EActiveTimerReturnType SKzDialogueLinePicker::FocusSearchBoxOnce(double, float)
{
	if (SearchBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox.ToSharedRef());
	}
	return EActiveTimerReturnType::Stop;
}

TSharedRef<ITableRow> SKzDialogueLinePicker::OnGenerateRow(KzDialoguePickerInternal::FEntryPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	if (Item->bIsHeader)
	{
		return SNew(STableRow<KzDialoguePickerInternal::FEntryPtr>, Owner)
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.NoHoverTableRow"))
			.ShowSelection(false)
			.Padding(FMargin(0, 6, 0, 2))
			[
				SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 6, 0)
					[
						SNew(STextBlock)
							.Text(FText::FromString(Item->DisplayText.ToUpper()))
							.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("Menu.Heading"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SSeparator)
							.SeparatorImage(FAppStyle::Get().GetBrush("Menu.Separator"))
							.Orientation(Orient_Horizontal)
							.Thickness(1.f)
					]
			];
	}

	const FSlateBrush* IconBrush = Item->bIsAlias
		? FAppStyle::GetBrush("Sequencer.KeyDiamond")
		: FAppStyle::GetBrush("ClassIcon.SoundCue");

	return SNew(STableRow<KzDialoguePickerInternal::FEntryPtr>, Owner)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 2.f)
				[
					SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
						[
							SNew(SImage).Image(IconBrush).ColorAndOpacity(FSlateColor::UseForeground())
						]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Item->DisplayText))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
		];
}

void SKzDialogueLinePicker::OnItemClicked(KzDialoguePickerInternal::FEntryPtr Item)
{
	if (!Item.IsValid() || Item->bIsHeader) { return; }

	FKzDialogueAssetReference Ref;
	Ref.Id = Item->Id;
	Ref.bIsAlias = Item->bIsAlias;

	OnEntryPicked.ExecuteIfBound(Ref, Item->DefaultDuration);
}

#undef LOCTEXT_NAMESPACE