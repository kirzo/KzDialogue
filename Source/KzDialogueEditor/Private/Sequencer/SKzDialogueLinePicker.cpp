// Copyright 2026 kirzo

#include "Sequencer/SKzDialogueLinePicker.h"
#include "KzDialogueAsset.h"
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

namespace
{
	/** Sentinel tag used to represent "no speaker / narration" in filter sets. */
	static const FGameplayTag& GetNarrationSentinel()
	{
		// An invalid (default-constructed) tag is used as the narration sentinel. It can
		// safely live in TSet/TArray and compares equal to other default-constructed tags.
		static const FGameplayTag Sentinel;
		return Sentinel;
	}
}

void SKzDialogueLinePicker::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	OnLinePicked = InArgs._OnLinePicked;
	AlreadyUsedLineIds = InArgs._AlreadyUsedLineIds;
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
									SAssignNew(ListView, SListView<KzDialoguePickerInternal::FLineEntryPtr>)
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

	AllItems.Reserve(AssetPtr->Lines.Num());
	for (int32 i = 0; i < AssetPtr->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = AssetPtr->Lines[i];

		// Resolve the default section duration once: explicit > audio length > 2s.
		float Duration = Line.Duration;
		if (Duration <= 0.0f)
		{
			if (USoundBase* Sound = Line.Audio.LoadSynchronous())
			{
				Duration = Sound->GetDuration();
			}
		}
		if (Duration <= 0.0f) { Duration = 2.0f; }

		const FString TextStr = Line.Text.ToString();
		const FString SpeakerStr = Line.Speaker.SpeakerTag.IsValid() ? Line.Speaker.SpeakerTag.ToString() : FString();

		KzDialoguePickerInternal::FLineEntryPtr Entry = MakeShared<KzDialoguePickerInternal::FLineEntry>();
		Entry->LineId = Line.LineId;
		Entry->DisplayText = FString::Printf(TEXT("%d. %s"), i + 1, *Line.GetDisplayLabel(45).ToString());
		Entry->SearchHaystack = (SpeakerStr + TEXT(" ") + TextStr).ToLower();
		Entry->DefaultDuration = Duration;
		Entry->SpeakerTag = Line.Speaker.SpeakerTag;
		Entry->LineTags = Line.Tags;
		Entry->bHasAudio = !Line.Audio.IsNull();

		AllItems.Add(Entry);
	}
}

void SKzDialogueLinePicker::GatherDistinctSpeakers()
{
	DistinctSpeakerTags.Reset();
	SpeakerDisplayNames.Reset();

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!IsValid(AssetPtr)) { return; }

	// Collect distinct speakers, preserving insertion order for a stable filter UI.
	// At the same time, capture the first non-empty DisplayNameOverride per speaker so
	// we can show "TagShortName (DisplayName)" in the menu.
	TSet<FGameplayTag> Seen;
	for (const FKzDialogueLine& Line : AssetPtr->Lines)
	{
		const FGameplayTag Key = Line.Speaker.SpeakerTag.IsValid()
			? Line.Speaker.SpeakerTag
			: GetNarrationSentinel();

		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			DistinctSpeakerTags.Add(Key);
		}

		if (!Line.Speaker.DisplayNameOverride.IsEmpty() && !SpeakerDisplayNames.Contains(Key))
		{
			SpeakerDisplayNames.Add(Key, Line.Speaker.DisplayNameOverride.ToString());
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
	VisibleItems.Reserve(AllItems.Num());

	for (const KzDialoguePickerInternal::FLineEntryPtr& Item : AllItems)
	{
		// 1. Search text
		if (!CurrentSearchText.IsEmpty() && !Item->SearchHaystack.Contains(CurrentSearchText))
		{
			continue;
		}

		// 2. Speakers
		if (SelectedSpeakerFilters.Num() > 0)
		{
			const FGameplayTag Key = Item->SpeakerTag.IsValid() ? Item->SpeakerTag : GetNarrationSentinel();
			if (!SelectedSpeakerFilters.Contains(Key)) { continue; }
		}

		// 3. Line tags (line passes if it has at least one of the selected tags)
		if (SelectedLineTagFilters.Num() > 0)
		{
			bool bMatch = false;
			for (const FGameplayTag& Wanted : SelectedLineTagFilters)
			{
				if (Item->LineTags.HasTagExact(Wanted)) { bMatch = true; break; }
			}
			if (!bMatch) { continue; }
		}

		// 4. Hide already-used
		if (bHideAlreadyUsed && AlreadyUsedLineIds.Contains(Item->LineId))
		{
			continue;
		}

		VisibleItems.Add(Item);
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
	if (DistinctSpeakerTags.Num() > 1)
	{
		MenuBuilder.BeginSection("KzDialoguePicker_Speakers", LOCTEXT("SpeakersSection", "Speakers"));
		for (const FGameplayTag& Speaker : DistinctSpeakerTags)
		{
			const FText Label = GetSpeakerDisplayLabel(Speaker);
			MenuBuilder.AddMenuEntry(
				Label,
				FText::Format(LOCTEXT("FilterBySpeakerTip", "Show only lines spoken by {0}"), Label),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
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
		DistinctSpeakerTags.Num() > 1 ||
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

void SKzDialogueLinePicker::ToggleSpeakerFilter(FGameplayTag SpeakerTag)
{
	if (SelectedSpeakerFilters.Contains(SpeakerTag)) { SelectedSpeakerFilters.Remove(SpeakerTag); }
	else { SelectedSpeakerFilters.Add(SpeakerTag); }
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

FText SKzDialogueLinePicker::GetSpeakerDisplayLabel(FGameplayTag SpeakerTag) const
{
	if (!SpeakerTag.IsValid())
	{
		return NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
	}

	// Build a synthetic speaker that combines the tag with the display name we cached
	// while gathering distinct speakers. Lets us reuse the canonical label resolution.
	FKzDialogueSpeaker Synthetic;
	Synthetic.SpeakerTag = SpeakerTag;

	if (const FString* DisplayName = SpeakerDisplayNames.Find(SpeakerTag))
	{
		// In the filter menu we want to surface both: the short tag and the override
		// when they differ, so the user can disambiguate two NPCs with the same display
		// name but different tags.
		const FText Short = Synthetic.GetDisplayLabel(); // resolves to the leaf
		return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *Short.ToString(), **DisplayName));
	}

	return Synthetic.GetDisplayLabel();
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

TSharedRef<ITableRow> SKzDialogueLinePicker::OnGenerateRow(KzDialoguePickerInternal::FLineEntryPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<KzDialoguePickerInternal::FLineEntryPtr>, Owner)
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SBox)
				.Padding(FMargin(8.f, 4.f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(Item->DisplayText))
				]
		];
}

void SKzDialogueLinePicker::OnItemClicked(KzDialoguePickerInternal::FLineEntryPtr Item)
{
	if (Item.IsValid() && OnLinePicked.IsBound())
	{
		OnLinePicked.Execute(Item->LineId, Item->DefaultDuration);
	}
	FSlateApplication::Get().DismissAllMenus();
}

#undef LOCTEXT_NAMESPACE