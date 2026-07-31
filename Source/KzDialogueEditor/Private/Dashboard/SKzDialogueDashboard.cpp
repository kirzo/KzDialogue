// Copyright 2026 kirzo

#include "Dashboard/SKzDialogueDashboard.h"

#include "KzDialogueAsset.h"
#include "KzDialogueEditorStyle.h"
#include "KzSpeakerAsset.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/SKzDialogueCoveragePanel.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "LocalizationCommandletTasks.h"
#include "LocalizationModule.h"
#include "Logging/MessageLog.h"
#include "Misc/ScopedSlowTask.h"
#include "Sound/SoundBase.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "KzDialogueDashboard"

const FName SKzDialogueDashboard::TabId(TEXT("KzDialogueDashboard"));

namespace KzDashboardCol
{
	static const FName Asset(TEXT("Asset"));
	static const FName Lines(TEXT("Lines"));
	static const FName Voiced(TEXT("Voiced"));
	static const FName Speakers(TEXT("Speakers"));
}

namespace
{
	void ShowDashboardNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** Multi-column row for assets and lines. Cells capture the culture active at generation
	 *  time; every culture change rebuilds the list, so they never go stale. */
	class SKzDashboardRow : public SMultiColumnTableRow<SKzDialogueDashboard::FRowPtr>
	{
	public:
		SLATE_BEGIN_ARGS(SKzDashboardRow) {}
			SLATE_ARGUMENT(FString, Culture)
			SLATE_ARGUMENT(bool, bNativeCulture)
			SLATE_ARGUMENT(TSharedPtr<SKzDialogueDashboard::FAudition>, Audition)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, SKzDialogueDashboard::FRowPtr InRow)
		{
			Row = InRow;
			Culture = InArgs._Culture;
			bNativeCulture = InArgs._bNativeCulture;
			Audition = InArgs._Audition;
			SMultiColumnTableRow<SKzDialogueDashboard::FRowPtr>::Construct(FSuperRowType::FArguments(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			auto Cell = [](const FText& Value, const FLinearColor* Color = nullptr, const FText& Tip = FText()) -> TSharedRef<SWidget>
			{
				TSharedRef<STextBlock> Text = SNew(STextBlock).Text(Value);
				if (Color) { Text->SetColorAndOpacity(FSlateColor(*Color)); }
				if (!Tip.IsEmpty()) { Text->SetToolTipText(Tip); }
				return SNew(SBox).Padding(4.f, 2.f).VAlign(VAlign_Center)[ Text ];
			};

			if (!Row.IsValid()) { return SNullWidget::NullWidget; }

			if (Column == KzDashboardCol::Asset)
			{
				if (Row->bIsLine)
				{
					// Truncated line text; full text in the tooltip.
					FString Preview = Row->LineText;
					if (Preview.Len() > 90) { Preview = Preview.Left(90) + TEXT("..."); }

					TSharedRef<SHorizontalBox> LineBox = SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SExpanderArrow, SharedThis(this))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SBox).Padding(4.f, 2.f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(Preview))
									.ToolTipText(FText::FromString(Row->LineText))
							]
						];

					if (Row->bLineVoiced)
					{
						// Same audition mechanic as the asset editor's per-line play button.
						LineBox->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ToolTipText(LOCTEXT("AuditionLine", "Play this line's audio"))
								.OnClicked(this, &SKzDashboardRow::OnPlayClicked)
								[
									SNew(SImage)
										.Image_Lambda([this]() { return FAppStyle::GetBrush(IsAuditioningThis() ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"); })
										.ColorAndOpacity(FSlateColor::UseForeground())
								]
						];
					}

					return LineBox;
				}

				// Asset row: display label (asset name + package in the tooltip) plus a magnifier
				// that syncs the main Content Browser (bAllowLockedBrowsers keeps a locked primary
				// usable instead of spawning a floating one).
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SExpanderArrow, SharedThis(this))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SBox).Padding(4.f, 2.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(Row->AssetLabel()))
								.ToolTipText(FText::Format(LOCTEXT("AssetTip", "{0}\n{1}"), FText::FromName(Row->Asset.AssetName), FText::FromName(Row->Asset.PackageName)))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ToolTipText(LOCTEXT("BrowseToAsset", "Find in Content Browser"))
							.OnClicked_Lambda([Data = Row->Asset]()
							{
								FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								ContentBrowser.Get().SyncBrowserToAssets(TArray<FAssetData>{ Data }, /*bAllowLockedBrowsers=*/true);
								return FReply::Handled();
							})
							[
								SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.Search"))
									.ColorAndOpacity(FSlateColor::UseForeground())
							]
					];
			}

			static const FLinearColor DoneColor(0.35f, 0.8f, 0.35f);
			static const FLinearColor StaleColor(0.9f, 0.75f, 0.3f);
			static const FLinearColor MissingColor(0.9f, 0.35f, 0.3f);

			if (Column == KzDashboardCol::Lines)
			{
				if (!Row->bIsLine)
				{
					if (Culture.IsEmpty())
					{
						return Cell(Row->LineCount >= 0 ? FText::AsNumber(Row->LineCount) : LOCTEXT("UnknownCount", "?"));
					}
					// Native text is the source itself: nothing to translate into.
					if (bNativeCulture) { return Cell(LOCTEXT("NoValue", "-")); }
					if (const FIntPoint* Rollup = Row->TextRollup.Find(Culture))
					{
						const bool bDone = Rollup->X >= Rollup->Y;
						return Cell(FText::Format(LOCTEXT("RollupCell", "{0}/{1}"), Rollup->X, Rollup->Y), bDone ? &DoneColor : &MissingColor);
					}
					return Cell(LOCTEXT("UnknownCount", "?"));
				}

				if (Culture.IsEmpty() || bNativeCulture || Row->LineKey.IsEmpty()) { return Cell(LOCTEXT("NoValue", "-")); }
				if (const EKzTranslationState* State = Row->LineTextState.Find(Culture))
				{
					switch (*State)
					{
					case EKzTranslationState::Translated: return Cell(LOCTEXT("StateOk", "ok"), &DoneColor);
					case EKzTranslationState::Stale: return Cell(LOCTEXT("StateStale", "stale"), &StaleColor);
					default: return Cell(LOCTEXT("StateMissing", "missing"), &MissingColor);
					}
				}
				return Cell(LOCTEXT("UnknownCount", "?"));
			}

			if (Column == KzDashboardCol::Voiced)
			{
				if (!Row->bIsLine)
				{
					if (Culture.IsEmpty())
					{
						return Cell(Row->VoicedCount >= 0 ? FText::AsNumber(Row->VoicedCount) : LOCTEXT("UnknownCount", "?"));
					}
					if (const FIntPoint* Rollup = Row->AudioRollup.Find(Culture))
					{
						const bool bDone = Rollup->X >= Rollup->Y;
						return Cell(FText::Format(LOCTEXT("RollupCell", "{0}/{1}"), Rollup->X, Rollup->Y), bDone ? &DoneColor : &MissingColor);
					}
					return Cell(LOCTEXT("UnknownCount", "?"));
				}

				const FText StaleVoTip = LOCTEXT("StaleVoTip", "Text changed after this audio was recorded. Re-record it, or re-assign the audio to accept the current text.");

				if (Culture.IsEmpty())
				{
					if (!Row->bLineVoiced) { return Cell(LOCTEXT("NoValue", "-")); }
					return Row->bLineAudioStale
						? Cell(LOCTEXT("StateStale", "stale"), &StaleColor, StaleVoTip)
						: Cell(LOCTEXT("VoicedYes", "yes"));
				}

				// Native: recording state of the source audio.
				if (bNativeCulture)
				{
					if (!Row->bLineVoiced) { return Cell(LOCTEXT("StateMissing", "missing"), &MissingColor); }
					return Row->bLineAudioStale
						? Cell(LOCTEXT("StateStale", "stale"), &StaleColor, StaleVoTip)
						: Cell(LOCTEXT("StateOk", "ok"), &DoneColor);
				}

				if (!Row->bLineVoiced) { return Cell(LOCTEXT("NoValue", "-")); }
				if (const bool* bLocalized = Row->LineAudioLocalized.Find(Culture))
				{
					return *bLocalized ? Cell(LOCTEXT("StateOk", "ok"), &DoneColor) : Cell(LOCTEXT("StateMissing", "missing"), &MissingColor);
				}
				return Cell(LOCTEXT("UnknownCount", "?"));
			}

			if (Column == KzDashboardCol::Speakers)
			{
				return Cell(FText::FromString(Row->bIsLine ? Row->LineSpeaker : Row->Speakers.Replace(TEXT(";"), TEXT(", "))));
			}

			return SNullWidget::NullWidget;
		}

	private:
		bool bNativeCulture = false;

		bool IsAuditioningThis() const
		{
			return Audition.IsValid() && Row.IsValid()
				&& Audition->LineId == Row->LineId
				&& Audition->Component.IsValid() && Audition->Component->IsPlaying();
		}

		FReply OnPlayClicked()
		{
			// Toggle: stop whatever is playing; start this line unless it was the one playing.
			const bool bWasThis = IsAuditioningThis();
			if (GEditor) { GEditor->ResetPreviewAudioComponent(); }
			Audition->Component = nullptr;
			Audition->LineId = FGuid();

			if (!bWasThis && GEditor && Row.IsValid())
			{
				if (USoundBase* Sound = Cast<USoundBase>(Row->LineAudioPath.TryLoad()))
				{
					Audition->Component = GEditor->PlayPreviewSound(Sound);
					Audition->LineId = Row->LineId;
				}
			}
			return FReply::Handled();
		}

		SKzDialogueDashboard::FRowPtr Row;
		FString Culture;
		TSharedPtr<SKzDialogueDashboard::FAudition> Audition;
	};
}

void SKzDialogueDashboard::Construct(const FArguments& /*InArgs*/)
{
	SortColumn = KzDashboardCol::Asset;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar: filters on the left, batch actions on the right.
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Search... (with In Lines: press Enter to search line texts)"))
					.OnTextChanged_Lambda([this](const FText& Text) { FilterText = Text.ToString(); RebuildVisible(); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
					{
						FilterText = Text.ToString();
						if (CommitType == ETextCommit::OnEnter && bSearchInLines && !FilterText.IsEmpty())
						{
							// Full-text sweep: load every asset's lines once, then filter and
							// expand the matches.
							AnalyzeFiltered(LOCTEXT("LoadingLines", "Loading dialogue lines..."));
							RebuildVisible();
							for (const FRowPtr& Row : VisibleRows)
							{
								if (Row.IsValid() && Row->bChildrenLoaded && TreeView.IsValid())
								{
									TreeView->SetItemExpansion(Row, true);
								}
							}
							return;
						}
						RebuildVisible();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSearchInLines ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSearchInLines = State == ECheckBoxState::Checked; RebuildVisible(); })
					.ToolTipText(LOCTEXT("SearchInLinesTip", "Also search inside line texts. Loads the dialogue assets when you press Enter."))
					[
						SNew(STextBlock).Text(LOCTEXT("SearchInLines", "In lines"))
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildSpeakerFilterMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Text_Lambda([this]() { return SpeakerFilter.IsEmpty() ? LOCTEXT("AllSpeakers", "All Speakers") : FText::FromString(SpeakerFilter); })
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildCultureMenu)
					.ToolTipText(LOCTEXT("CultureTip", "Reframe the dashboard to one culture: per-line text/audio states and per-asset progress."))
					.ButtonContent()
					[
						SNew(STextBlock).Text_Lambda([this]() { return SelectedCulture.IsEmpty() ? LOCTEXT("AllCultures", "All Cultures") : FKzDialogueTranslationCsv::GetCultureDisplayLabel(SelectedCulture); })
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildFiltersMenu)
					.ToolTipText(LOCTEXT("FiltersTip", "Row filters"))
					.ButtonContent()
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Filter"))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildViewOptionsMenu)
					.ToolTipText(LOCTEXT("ViewOptionsTip", "Column visibility"))
					.ButtonContent()
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("GenericViewButton"))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.ToolTipText(LOCTEXT("RefreshTip", "Re-read the asset registry and the localization archives, dropping every cache."))
					.OnClicked_Lambda([this]()
					{
						LocQuery.Reset();
						AllRows.Reset();
						RebuildFromRegistry();
						if (!SelectedCulture.IsEmpty() && EnsureLocQuery())
						{
							AnalyzeFiltered(LOCTEXT("Analyzing", "Analyzing localization..."));
							RebuildVisible();
						}
						return FReply::Handled();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(16.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildExportMenu)
					.ToolTipText(LOCTEXT("ExportCsvTip", "Export dialogue assets to a translation CSV: the selection, the filtered set or everything."))
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("ExportCsv", "Export CSV"))
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueDashboard::BuildImportCultureMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("ImportCsv", "Import CSV"))
					]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
					.Text(LOCTEXT("Coverage", "Coverage"))
					.ToolTipText(LOCTEXT("CoverageTip", "Load the FILTERED assets and show their translation coverage across every culture."))
					.OnClicked(this, &SKzDialogueDashboard::OnCoverageClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(16.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
					.Text(LOCTEXT("GatherText", "Gather"))
					.ToolTipText(LOCTEXT("GatherTextTip", "Run Gather Text on the localization target, same as the Localization Dashboard button. Refreshes manifest and archives from saved assets."))
					.OnClicked(this, &SKzDialogueDashboard::OnGatherClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
					.Text(LOCTEXT("CompileText", "Compile"))
					.ToolTipText(LOCTEXT("CompileTextTip", "Run Compile Text on the localization target: writes the .locres files the game reads at runtime."))
					.OnClicked(this, &SKzDialogueDashboard::OnCompileClicked)
			]
		]

		// Visible / total counter.
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 4.f)
		[
			SNew(STextBlock)
				.Text_Lambda([this]() { return FText::Format(LOCTEXT("CountLabel", "{0} of {1} dialogue assets"), VisibleRows.Num(), AllRows.Num()); })
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		// The tree.
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f, 0.f, 6.f, 6.f)
		[
			SAssignNew(TreeView, STreeView<FRowPtr>)
				.TreeItemsSource(&VisibleRows)
				.OnGenerateRow(this, &SKzDialogueDashboard::OnGenerateRow)
				.OnGetChildren(this, &SKzDialogueDashboard::OnGetChildren)
				.OnExpansionChanged(this, &SKzDialogueDashboard::OnExpansionChanged)
				.OnMouseButtonDoubleClick(this, &SKzDialogueDashboard::OnRowDoubleClicked)
				.SelectionMode(ESelectionMode::Multi)
				.HeaderRow(
					SAssignNew(HeaderRow, SHeaderRow)
					+ SHeaderRow::Column(KzDashboardCol::Asset)
						.DefaultLabel(LOCTEXT("ColAsset", "Asset")).FillWidth(0.5f)
						.SortMode_Lambda([this]() { return GetColumnSortMode(KzDashboardCol::Asset); })
						.OnSort(this, &SKzDialogueDashboard::OnColumnSort)
					+ SHeaderRow::Column(KzDashboardCol::Lines)
						.DefaultLabel_Lambda([this]() { return SelectedCulture.IsEmpty() ? LOCTEXT("ColLines", "Lines") : FText::Format(LOCTEXT("ColTextCulture", "Text ({0})"), FText::FromString(SelectedCulture)); })
						.FixedWidth(80.f)
						.SortMode_Lambda([this]() { return GetColumnSortMode(KzDashboardCol::Lines); })
						.OnSort(this, &SKzDialogueDashboard::OnColumnSort)
					+ SHeaderRow::Column(KzDashboardCol::Voiced)
						.DefaultLabel_Lambda([this]() { return SelectedCulture.IsEmpty() ? LOCTEXT("ColVoiced", "Voiced") : FText::Format(LOCTEXT("ColAudioCulture", "Audio ({0})"), FText::FromString(SelectedCulture)); })
						.FixedWidth(80.f)
						.ShouldGenerateWidget_Lambda([this]() { return bShowVoicedColumn; })
						.SortMode_Lambda([this]() { return GetColumnSortMode(KzDashboardCol::Voiced); })
						.OnSort(this, &SKzDialogueDashboard::OnColumnSort)
					+ SHeaderRow::Column(KzDashboardCol::Speakers)
						.DefaultLabel(LOCTEXT("ColSpeakers", "Speakers")).FillWidth(0.25f)
						.SortMode_Lambda([this]() { return GetColumnSortMode(KzDashboardCol::Speakers); })
						.OnSort(this, &SKzDialogueDashboard::OnColumnSort)
				)
		]

		// Coverage footer, empty until the Coverage action runs.
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 6.f)
		[
			SNew(SBox).MaxDesiredHeight(180.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(CoverageRows, SVerticalBox)
				]
			]
		]
	];

	// Populate now and keep in sync with the registry (adds, deletes, renames, resaves).
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.OnFilesLoaded().AddSP(this, &SKzDialogueDashboard::OnRegistryFilesLoaded);
	Registry.OnAssetAdded().AddSP(this, &SKzDialogueDashboard::OnAssetRegistryChanged);
	Registry.OnAssetRemoved().AddSP(this, &SKzDialogueDashboard::OnAssetRegistryChanged);
	Registry.OnAssetUpdated().AddSP(this, &SKzDialogueDashboard::OnAssetRegistryChanged);
	Registry.OnAssetRenamed().AddSP(this, &SKzDialogueDashboard::OnAssetRenamed);

	RebuildFromRegistry();
}

SKzDialogueDashboard::~SKzDialogueDashboard()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.OnFilesLoaded().RemoveAll(this);
		Registry.OnAssetAdded().RemoveAll(this);
		Registry.OnAssetRemoved().RemoveAll(this);
		Registry.OnAssetUpdated().RemoveAll(this);
		Registry.OnAssetRenamed().RemoveAll(this);
	}
}

void SKzDialogueDashboard::RebuildFromRegistry(FName InvalidatedPackage)
{
	// Carry over expensive per-row caches (loaded children, culture data) by package, except
	// for the asset that changed. Remember which rows were expanded to restore afterwards.
	TMap<FName, FRowPtr> Previous;
	for (const FRowPtr& Row : AllRows)
	{
		if (Row.IsValid()) { Previous.Add(Row->Asset.PackageName, Row); }
	}

	TSet<FName> ExpandedPackages;
	if (TreeView.IsValid())
	{
		TSet<FRowPtr> ExpandedItems;
		TreeView->GetExpandedItems(ExpandedItems);
		for (const FRowPtr& Row : ExpandedItems)
		{
			if (Row.IsValid() && !Row->bIsLine) { ExpandedPackages.Add(Row->Asset.PackageName); }
		}
	}

	AllRows.Reset();

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Found;
	Registry.GetAssetsByClass(UKzDialogueAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);

	AllRows.Reserve(Found.Num());
	for (FAssetData& Data : Found)
	{
		FRowPtr Row = MakeShared<FRow>();
		// GetTagValue leaves the out value untouched when the tag is absent (-1 / empty = unknown).
		Data.GetTagValue(UKzDialogueAsset::TagLineCount, Row->LineCount);
		Data.GetTagValue(UKzDialogueAsset::TagVoicedCount, Row->VoicedCount);
		Data.GetTagValue(UKzDialogueAsset::TagSpeakers, Row->Speakers);
		Data.GetTagValue(UKzDialogueAsset::TagDisplayName, Row->DisplayName);

		if (const FRowPtr* Old = Previous.Find(Data.PackageName); Old && (*Old).IsValid() && Data.PackageName != InvalidatedPackage)
		{
			Row->Children = (*Old)->Children;
			Row->bChildrenLoaded = (*Old)->bChildrenLoaded;
			Row->TextRollup = (*Old)->TextRollup;
			Row->AudioRollup = (*Old)->AudioRollup;
		}

		Row->Asset = MoveTemp(Data);
		AllRows.Add(MoveTemp(Row));
	}

	RebuildVisible();

	if (TreeView.IsValid())
	{
		for (const FRowPtr& Row : AllRows)
		{
			if (Row.IsValid() && Row->bChildrenLoaded && ExpandedPackages.Contains(Row->Asset.PackageName))
			{
				TreeView->SetItemExpansion(Row, true);
			}
		}
	}
}

void SKzDialogueDashboard::RebuildVisible()
{
	VisibleRows.Reset();
	for (const FRowPtr& Row : AllRows)
	{
		if (Row.IsValid() && PassesFilters(*Row))
		{
			VisibleRows.Add(Row);
		}
	}

	const bool bAscending = SortMode == EColumnSortMode::Ascending;
	const FName Column = SortColumn;
	const FString Culture = SelectedCulture;
	VisibleRows.Sort([Column, bAscending, &Culture](const FRowPtr& A, const FRowPtr& B)
	{
		// In culture mode the count columns sort by pending work; unanalyzed rows group first.
		auto TextPending = [&Culture](const FRowPtr& Row)
		{
			const FIntPoint* Rollup = Row->TextRollup.Find(Culture);
			return Rollup ? Rollup->Y - Rollup->X : MAX_int32;
		};
		auto AudioPending = [&Culture](const FRowPtr& Row)
		{
			const FIntPoint* Rollup = Row->AudioRollup.Find(Culture);
			return Rollup ? Rollup->Y - Rollup->X : MAX_int32;
		};

		int32 Compare = 0;
		if (Column == KzDashboardCol::Asset) { Compare = A->AssetLabel().Compare(B->AssetLabel()); }
		else if (Column == KzDashboardCol::Lines) { Compare = Culture.IsEmpty() ? A->LineCount - B->LineCount : TextPending(A) - TextPending(B); }
		else if (Column == KzDashboardCol::Voiced) { Compare = Culture.IsEmpty() ? A->VoicedCount - B->VoicedCount : AudioPending(A) - AudioPending(B); }
		else if (Column == KzDashboardCol::Speakers) { Compare = A->Speakers.Compare(B->Speakers); }
		else { Compare = A->Asset.PackageName.Compare(B->Asset.PackageName); }

		// Deterministic tiebreaker so equal keys keep a stable order.
		if (Compare == 0) { Compare = A->Asset.PackageName.Compare(B->Asset.PackageName); }
		return bAscending ? Compare < 0 : Compare > 0;
	});

	if (TreeView.IsValid())
	{
		// RebuildList drops the cached row widgets; only the visible ones regenerate, so the
		// cost is a handful of rows and cells never survive a column or culture change stale.
		TreeView->RebuildList();
	}
}

bool SKzDialogueDashboard::PassesFilters(const FRow& Row) const
{
	const FString PackagePath = Row.Asset.PackagePath.ToString();

	if (!bShowDevelopers && PackagePath.StartsWith(TEXT("/Game/Developers")))
	{
		return false;
	}

	// Rows with an unknown speaker list (asset saved before the tags existed) pass the
	// speaker filter: unknown is not the same as excluded.
	if (!SpeakerFilter.IsEmpty() && !Row.Speakers.IsEmpty())
	{
		const FString Wrapped = TEXT(";") + Row.Speakers + TEXT(";");
		if (!Wrapped.Contains(TEXT(";") + SpeakerFilter + TEXT(";")))
		{
			return false;
		}
	}

	if (bOnlyIncomplete && !SelectedCulture.IsEmpty())
	{
		// Unanalyzed rows stay visible ("?"): unknown is not the same as complete.
		const FIntPoint* Text = Row.TextRollup.Find(SelectedCulture);
		const FIntPoint* Audio = Row.AudioRollup.Find(SelectedCulture);
		if (Text && Audio)
		{
			const bool bTextDone = Text->X >= Text->Y;
			const bool bAudioDone = !bShowVoicedColumn || Audio->X >= Audio->Y;
			if (bTextDone && bAudioDone) { return false; }
		}
	}

	if (bOnlyMissingVoice)
	{
		if (Row.bChildrenLoaded)
		{
			bool bAnyMissing = false;
			for (const FRowPtr& Child : Row.Children)
			{
				if (Child.IsValid() && IsLineMissingVoice(*Child) && (SpeakerFilter.IsEmpty() || Child->LineSpeakerAssetName == SpeakerFilter))
				{
					bAnyMissing = true;
					break;
				}
			}
			if (!bAnyMissing) { return false; }
		}
		else if (!SelectedCulture.IsEmpty())
		{
			// Unloaded but analyzed rows keep their rollup; no rollup = unknown, stays visible.
			if (const FIntPoint* Audio = Row.AudioRollup.Find(SelectedCulture))
			{
				if (Audio->X >= Audio->Y) { return false; }
			}
		}
		else if (Row.LineCount >= 0 && Row.VoicedCount >= 0 && Row.VoicedCount >= Row.LineCount)
		{
			// The tags say every line is voiced (drift is invisible to tags). Unknown counts stay visible.
			return false;
		}
	}

	if (!FilterText.IsEmpty())
	{
		const bool bAssetMatch = Row.AssetLabel().Contains(FilterText)
			|| Row.Asset.AssetName.ToString().Contains(FilterText)
			|| PackagePath.Contains(FilterText)
			|| Row.Speakers.Contains(FilterText);

		if (bAssetMatch) { return true; }

		if (bSearchInLines && Row.bChildrenLoaded)
		{
			for (const FRowPtr& Child : Row.Children)
			{
				if (Child.IsValid() && LineMatchesSearch(*Child)) { return true; }
			}
		}
		return false;
	}

	return true;
}

bool SKzDialogueDashboard::IsLineIncomplete(const FRow& Line) const
{
	const bool bNative = IsNativeSelected();

	// Native text is the source itself: nothing to translate.
	if (!bNative && !Line.LineKey.IsEmpty())
	{
		const EKzTranslationState* State = Line.LineTextState.Find(SelectedCulture);
		if (!State || *State != EKzTranslationState::Translated) { return true; }
	}

	// Audio only counts as pending work when the project localizes audio (Voiced column shown).
	if (bShowVoicedColumn)
	{
		if (bNative)
		{
			if (!Line.bLineVoiced || Line.bLineAudioStale) { return true; }
		}
		else if (Line.bLineVoiced)
		{
			const bool* bLocalized = Line.LineAudioLocalized.Find(SelectedCulture);
			if (!bLocalized || !*bLocalized) { return true; }
		}
	}

	return false;
}

bool SKzDialogueDashboard::IsLineMissingVoice(const FRow& Line) const
{
	// No culture / native: recording work on the source audio (missing take or drifted text).
	if (SelectedCulture.IsEmpty() || IsNativeSelected())
	{
		return !Line.bLineVoiced || Line.bLineAudioStale;
	}

	// Foreign culture: the localized variant is what matters. Unanalyzed falls back to source.
	const bool* bLocalized = Line.LineAudioLocalized.Find(SelectedCulture);
	return bLocalized ? !*bLocalized : !Line.bLineVoiced;
}

bool SKzDialogueDashboard::LineMatchesSearch(const FRow& Line) const
{
	return Line.LineText.Contains(FilterText) || Line.LineSpeaker.Contains(FilterText);
}

void SKzDialogueDashboard::LoadChildren(const FRowPtr& AssetRow)
{
	if (!AssetRow.IsValid() || AssetRow->bIsLine || AssetRow->bChildrenLoaded) { return; }
	AssetRow->bChildrenLoaded = true;

	// Drop the lazy-tree placeholder before filling in the real lines.
	AssetRow->Children.Reset();

	UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(AssetRow->Asset.GetAsset());
	if (!Asset) { return; }

	AssetRow->Children.Reserve(Asset->Lines.Num());
	for (const FKzDialogueLine& Line : Asset->Lines)
	{
		FRowPtr Child = MakeShared<FRow>();
		Child->bIsLine = true;
		Child->Asset = AssetRow->Asset;
		Child->LineId = Line.LineId;

		const FString* Source = FTextInspector::GetSourceString(Line.Text);
		Child->LineText = Source ? *Source : Line.Text.ToString();
		Child->LineSpeaker = Line.Speaker.IsValid() ? Line.Speaker.GetDisplayLabel().ToString() : FString();
		Child->LineSpeakerAssetName = Line.Speaker.Asset ? Line.Speaker.Asset->GetName() : FString();
		Child->bLineVoiced = !Line.Audio.IsNull();
		if (Child->bLineVoiced)
		{
			Child->bLineAudioStale = Line.RecordedTextHash != 0 && Line.RecordedTextHash != Line.SourceTextHash;
			Child->LineAudioPath = Line.Audio.ToSoftObjectPath();
			Child->LineAudioPackage = Child->LineAudioPath.GetLongPackageName();
		}

		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
		const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
		if (Namespace.IsSet() && Key.IsSet() && !Line.Text.IsEmpty())
		{
			Child->LineNamespace = Namespace.GetValue();
			Child->LineKey = Key.GetValue();
		}

		AssetRow->Children.Add(MoveTemp(Child));
	}
}

void SKzDialogueDashboard::ComputeCultureData(const FRowPtr& AssetRow)
{
	if (!AssetRow.IsValid() || SelectedCulture.IsEmpty() || !LocQuery.IsValid()) { return; }
	if (AssetRow->TextRollup.Contains(SelectedCulture)) { return; }

	const bool bNative = IsNativeSelected();
	int32 TextDone = 0;
	int32 TextTotal = 0;
	int32 AudioDone = 0;
	int32 AudioTotal = 0;

	for (const FRowPtr& Child : AssetRow->Children)
	{
		if (!Child.IsValid()) { continue; }

		if (!bNative && !Child->LineKey.IsEmpty())
		{
			++TextTotal;
			const EKzTranslationState State = LocQuery->GetTextState(Child->LineNamespace, Child->LineKey, Child->LineText, SelectedCulture);
			Child->LineTextState.Add(SelectedCulture, State);
			if (State == EKzTranslationState::Translated) { ++TextDone; }
		}

		if (bNative)
		{
			// Recording state: every line counts, done = has audio and its text did not drift.
			++AudioTotal;
			if (Child->bLineVoiced && !Child->bLineAudioStale) { ++AudioDone; }
		}
		else if (Child->bLineVoiced)
		{
			++AudioTotal;
			const bool bLocalized = LocQuery->IsAudioLocalized(Child->LineAudioPackage, SelectedCulture);
			Child->LineAudioLocalized.Add(SelectedCulture, bLocalized);
			if (bLocalized) { ++AudioDone; }
		}
	}

	AssetRow->TextRollup.Add(SelectedCulture, FIntPoint(TextDone, TextTotal));
	AssetRow->AudioRollup.Add(SelectedCulture, FIntPoint(AudioDone, AudioTotal));
}

void SKzDialogueDashboard::AnalyzeFiltered(const FText& ProgressLabel)
{
	// Explicit action over the current view: loading these assets is the accepted cost.
	TArray<FRowPtr> Targets;
	for (const FRowPtr& Row : AllRows)
	{
		if (Row.IsValid() && PassesFilters(*Row))
		{
			Targets.Add(Row);
		}
	}
	if (Targets.Num() == 0) { return; }

	FScopedSlowTask SlowTask(static_cast<float>(Targets.Num()), ProgressLabel);
	SlowTask.MakeDialog();
	for (const FRowPtr& Row : Targets)
	{
		SlowTask.EnterProgressFrame(1.f);
		LoadChildren(Row);
		ComputeCultureData(Row);
	}
}

bool SKzDialogueDashboard::EnsureLocQuery()
{
	if (LocQuery.IsValid()) { return true; }

	TUniquePtr<FKzLocQuery> NewQuery = MakeUnique<FKzLocQuery>();
	FText Error;
	if (!NewQuery->Load(Error))
	{
		ShowDashboardNotification(Error, false);
		return false;
	}

	LocQuery = MoveTemp(NewQuery);
	NativeCulture = LocQuery->GetTarget().NativeCulture;
	return true;
}

TSharedRef<ITableRow> SKzDialogueDashboard::OnGenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SKzDashboardRow, Owner, Row)
		.Culture(SelectedCulture)
		.bNativeCulture(IsNativeSelected())
		.Audition(Audition);
}

void SKzDialogueDashboard::OnGetChildren(FRowPtr Row, TArray<FRowPtr>& OutChildren)
{
	if (!Row.IsValid() || Row->bIsLine) { return; }

	if (!Row->bChildrenLoaded)
	{
		// Lazy-tree placeholder: without a child the tree treats the row as a leaf and never
		// shows the expander arrow, so expansion (and the real load) could never happen.
		// Assets known to have zero lines stay leaves; unknown counts (-1) get the arrow.
		if (Row->LineCount != 0)
		{
			if (Row->Children.IsEmpty())
			{
				FRowPtr Placeholder = MakeShared<FRow>();
				Placeholder->bIsLine = true;
				Placeholder->Asset = Row->Asset;
				Placeholder->LineText = TEXT("...");
				Row->Children.Add(MoveTemp(Placeholder));
			}
			OutChildren.Append(Row->Children);
		}
		return;
	}

	for (const FRowPtr& Child : Row->Children)
	{
		if (!Child.IsValid()) { continue; }
		if (bOnlyIncomplete && !SelectedCulture.IsEmpty() && !IsLineIncomplete(*Child)) { continue; }
		if (bOnlyMissingVoice && !IsLineMissingVoice(*Child)) { continue; }
		if (!SpeakerFilter.IsEmpty() && Child->LineSpeakerAssetName != SpeakerFilter) { continue; }
		if (bSearchInLines && !FilterText.IsEmpty() && !LineMatchesSearch(*Child)) { continue; }
		OutChildren.Add(Child);
	}
}

void SKzDialogueDashboard::OnExpansionChanged(FRowPtr Row, bool bExpanded)
{
	if (bExpanded && Row.IsValid() && !Row->bIsLine && !Row->bChildrenLoaded)
	{
		LoadChildren(Row);
		ComputeCultureData(Row);
		if (TreeView.IsValid()) { TreeView->RequestTreeRefresh(); }
	}
}

void SKzDialogueDashboard::OnRowDoubleClicked(FRowPtr Row)
{
	if (!Row.IsValid() || !GEditor) { return; }

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Subsystem->OpenEditorForAsset(Row->Asset.GetSoftObjectPath());

	// For line rows, jump to that line inside the freshly opened (or focused) editor. Same
	// navigation the Validation panel uses: the row customizer resolves the LineId.
	if (Row->bIsLine)
	{
		if (UObject* Asset = Row->Asset.GetAsset())
		{
			IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false);
			if (Instance && Instance->GetEditorName() == TEXT("KzArrayAssetEditor"))
			{
				static_cast<FKzArrayAssetEditor*>(Instance)->SelectElementById(Row->LineId);
			}
		}
	}
}

EColumnSortMode::Type SKzDialogueDashboard::GetColumnSortMode(FName Column) const
{
	return Column == SortColumn ? SortMode : EColumnSortMode::None;
}

void SKzDialogueDashboard::OnColumnSort(EColumnSortPriority::Type /*Priority*/, const FName& Column, EColumnSortMode::Type Mode)
{
	SortColumn = Column;
	SortMode = Mode;
	RebuildVisible();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildSpeakerFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AllSpeakers", "All Speakers"),
		LOCTEXT("AllSpeakersTip", "Clear the speaker filter."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { SpeakerFilter.Reset(); RebuildVisible(); })));

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> SpeakerAssets;
	Registry.GetAssetsByClass(UKzSpeakerAsset::StaticClass()->GetClassPathName(), SpeakerAssets, /*bSearchSubClasses=*/true);

	TArray<FString> Names;
	Names.Reserve(SpeakerAssets.Num());
	for (const FAssetData& Data : SpeakerAssets)
	{
		Names.AddUnique(Data.AssetName.ToString());
	}
	Names.Sort();

	for (const FString& Name : Names)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Name),
			FText::Format(LOCTEXT("SpeakerFilterTip", "Show only dialogues with lines spoken by {0}."), FText::FromString(Name)),
			FSlateIcon(FKzDialogueEditorStyle::Get().GetStyleSetName(), "Kz.Dialogue.SpeakerIcon"),
			FUIAction(FExecuteAction::CreateLambda([this, Name]() { SpeakerFilter = Name; RebuildVisible(); })));
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildCultureMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AllCultures", "All Cultures"),
		LOCTEXT("AllCulturesTip", "Cheap registry view: counts from tags, no per-culture states."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { SelectedCulture.Reset(); RebuildVisible(); })));

	FKzLocTargetInfo Target;
	FText Error;
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoCultures", "No localization target cultures found"),
			Error,
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return MenuBuilder.MakeWidget();
	}
	NativeCulture = Target.NativeCulture;

	auto SelectCulture = [this](const FString& Culture)
	{
		if (!EnsureLocQuery()) { return; }
		SelectedCulture = Culture;
		AnalyzeFiltered(FText::Format(LOCTEXT("AnalyzingCulture", "Analyzing '{0}'..."), FText::FromString(Culture)));
		RebuildVisible();
	};

	// Native first: the source-language view, where the audio column shows recording state
	// (missing takes, takes whose text drifted after recording).
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("NativeCultureEntry", "{0} - native"), FKzDialogueTranslationCsv::GetCultureDisplayLabel(Target.NativeCulture)),
		LOCTEXT("NativeCultureTip", "Source-language view: per-line recording state (missing audio, or audio whose text changed after it was recorded)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([SelectCulture, Culture = Target.NativeCulture]() { SelectCulture(Culture); })));

	for (const FString& Culture : Target.ForeignCultures)
	{
		MenuBuilder.AddMenuEntry(
			FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
			FText::Format(LOCTEXT("CultureEntryTip", "Analyze the filtered assets and show text/audio progress for '{0}'."), FText::FromString(Culture)),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectCulture, Culture]() { SelectCulture(Culture); })));
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildFiltersMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MissingVoice", "Missing voice"),
		LOCTEXT("MissingVoiceTip", "Show only lines whose audio is missing or needs re-recording for the selected culture (source audio when no culture is selected). Combine with the speaker filter and Export CSV to build a recording script."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyMissingVoice = !bOnlyMissingVoice; RebuildVisible(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bOnlyMissingVoice; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OnlyIncomplete", "Only incomplete"),
		LOCTEXT("OnlyIncompleteTip", "Show only assets and lines with work left for the selected culture. Needs a culture selected."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyIncomplete = !bOnlyIncomplete; RebuildVisible(); }),
			FCanExecuteAction::CreateLambda([this]() { return !SelectedCulture.IsEmpty(); }),
			FIsActionChecked::CreateLambda([this]() { return bOnlyIncomplete; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Developers", "Developers"),
		LOCTEXT("DevelopersTip", "Show assets under /Game/Developers"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bShowDevelopers = !bShowDevelopers; RebuildVisible(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bShowDevelopers; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildViewOptionsMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("VoicedColumn", "Voiced column"),
		LOCTEXT("VoicedColumnTip", "Show audio counts and states. Hide it in projects that do not localize audio; hidden, audio never counts as pending work."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				bShowVoicedColumn = !bShowVoicedColumn;
				if (HeaderRow.IsValid()) { HeaderRow->RefreshColumns(); }
				// RebuildList, not a plain refresh: a reused multi-column row keeps its old
				// cell set, so the column change would never show.
				if (TreeView.IsValid()) { TreeView->RebuildList(); }
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bShowVoicedColumn; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildExportMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	const int32 SelectedCount = GetSelectedAssetData().Num();

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportSelection", "Selection ({0})"), SelectedCount),
		LOCTEXT("ExportSelectionTip", "Export only the assets of the selected rows."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { FKzDialogueTranslationCsv::ExportInteractive(GetSelectedAssetData()); }),
			FCanExecuteAction::CreateLambda([this]() { return GetSelectedAssetData().Num() > 0; })));

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportFiltered", "Filtered ({0})"), VisibleRows.Num()),
		LOCTEXT("ExportFilteredTip", "Export every asset passing the current filters."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { FKzDialogueTranslationCsv::ExportInteractive(GetFilteredAssetData()); }),
			FCanExecuteAction::CreateLambda([this]() { return VisibleRows.Num() > 0; })));

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportAll", "All ({0})"), AllRows.Num()),
		LOCTEXT("ExportAllTip", "Export every dialogue asset in the project, ignoring filters."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				TArray<FAssetData> All;
				All.Reserve(AllRows.Num());
				for (const FRowPtr& Row : AllRows) { if (Row.IsValid()) { All.Add(Row->Asset); } }
				FKzDialogueTranslationCsv::ExportInteractive(MoveTemp(All));
			}),
			FCanExecuteAction::CreateLambda([this]() { return AllRows.Num() > 0; })));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueDashboard::BuildImportCultureMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	FKzLocTargetInfo Target;
	FText Error;
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoCultures", "No localization target cultures found"),
			Error,
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return MenuBuilder.MakeWidget();
	}

	for (const FString& Culture : Target.ForeignCultures)
	{
		MenuBuilder.AddMenuEntry(
			FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
			FText::Format(LOCTEXT("ImportCultureTip", "Import a translated CSV into the '{0}' archive."), FText::FromString(Culture)),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Culture]()
			{
				FKzDialogueTranslationCsv::ImportInteractive(Culture);
				InvalidateLocData();
			})));
	}

	return MenuBuilder.MakeWidget();
}

TArray<FAssetData> SKzDialogueDashboard::GetFilteredAssetData() const
{
	TArray<FAssetData> Result;
	Result.Reserve(VisibleRows.Num());
	for (const FRowPtr& Row : VisibleRows)
	{
		if (Row.IsValid())
		{
			Result.Add(Row->Asset);
		}
	}
	return Result;
}

TArray<FAssetData> SKzDialogueDashboard::GetSelectedAssetData() const
{
	// Line rows resolve to their owning asset; duplicates collapse.
	TArray<FAssetData> Result;
	TSet<FName> Seen;
	if (TreeView.IsValid())
	{
		for (const FRowPtr& Row : TreeView->GetSelectedItems())
		{
			if (Row.IsValid() && !Seen.Contains(Row->Asset.PackageName))
			{
				Seen.Add(Row->Asset.PackageName);
				Result.Add(Row->Asset);
			}
		}
	}
	return Result;
}

FReply SKzDialogueDashboard::OnCoverageClicked()
{
	if (!CoverageRows.IsValid()) { return FReply::Handled(); }
	CoverageRows->ClearChildren();

	const TArray<FAssetData> Filtered = GetFilteredAssetData();
	if (Filtered.Num() == 0) { return FReply::Handled(); }

	// Explicit action: loading the filtered set is the accepted cost.
	TArray<UKzDialogueAsset*> Loaded;
	Loaded.Reserve(Filtered.Num());
	{
		FScopedSlowTask SlowTask(static_cast<float>(Filtered.Num()), LOCTEXT("LoadingAssets", "Loading dialogue assets..."));
		SlowTask.MakeDialog();
		for (const FAssetData& Data : Filtered)
		{
			SlowTask.EnterProgressFrame(1.f);
			if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset()))
			{
				Loaded.Add(Asset);
			}
		}
	}

	TArray<FKzCultureCoverage> Cultures;
	TArray<FKzStaleTranslation> Stale;
	FText Error;
	if (!FKzDialogueTranslationCsv::BuildCoverage(Loaded, Cultures, Stale, Error))
	{
		CoverageRows->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock).Text(Error).AutoWrapText(true)
		];
		return FReply::Handled();
	}

	CoverageRows->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
	[
		SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("CoverageHeader", "Coverage of {0} filtered asset(s):"), Loaded.Num()))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
	];

	if (Cultures.Num() == 0)
	{
		CoverageRows->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("NoForeignCultures", "The localization target has no foreign cultures."))
		];
		return FReply::Handled();
	}

	SKzDialogueCoveragePanel::FillCoverageRows(*CoverageRows, Cultures);

	if (Stale.Num() > 0)
	{
		FKzDialogueTranslationCsv::LogStaleTranslations(Stale);
		FMessageLog(TEXT("KzDialogueL10N")).Open(EMessageSeverity::Warning, false);
	}

	return FReply::Handled();
}

FReply SKzDialogueDashboard::OnGatherClicked()
{
	ULocalizationTarget* Target = ILocalizationModule::Get().GetLocalizationTargetByName(GetDefault<UKzDialogueSettings>()->LocalizationTargetName, /*bIsEngineTarget=*/false);
	if (!Target)
	{
		ShowDashboardNotification(LOCTEXT("NoLocTarget", "Localization target not found. Create it once in the Localization Dashboard (Tools menu)."), false);
		return FReply::Handled();
	}

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!Window.IsValid()) { Window = FSlateApplication::Get().GetActiveTopLevelWindow(); }
	if (!Window.IsValid()) { return FReply::Handled(); }

	if (LocalizationCommandletTasks::GatherTextForTarget(Window.ToSharedRef(), Target))
	{
		InvalidateLocData();
	}
	return FReply::Handled();
}

FReply SKzDialogueDashboard::OnCompileClicked()
{
	ULocalizationTarget* Target = ILocalizationModule::Get().GetLocalizationTargetByName(GetDefault<UKzDialogueSettings>()->LocalizationTargetName, /*bIsEngineTarget=*/false);
	if (!Target)
	{
		ShowDashboardNotification(LOCTEXT("NoLocTarget", "Localization target not found. Create it once in the Localization Dashboard (Tools menu)."), false);
		return FReply::Handled();
	}

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!Window.IsValid()) { Window = FSlateApplication::Get().GetActiveTopLevelWindow(); }
	if (!Window.IsValid()) { return FReply::Handled(); }

	// Compile only writes .locres; the archive-derived caches stay valid.
	LocalizationCommandletTasks::CompileTextForTarget(Window.ToSharedRef(), Target);
	return FReply::Handled();
}

void SKzDialogueDashboard::InvalidateLocData()
{
	LocQuery.Reset();
	for (const FRowPtr& Row : AllRows)
	{
		if (!Row.IsValid()) { continue; }
		Row->TextRollup.Reset();
		Row->AudioRollup.Reset();
		for (const FRowPtr& Child : Row->Children)
		{
			if (Child.IsValid()) { Child->LineTextState.Reset(); }
		}
	}
	if (!SelectedCulture.IsEmpty() && EnsureLocQuery())
	{
		AnalyzeFiltered(LOCTEXT("Analyzing", "Analyzing localization..."));
	}
	RebuildVisible();
}

void SKzDialogueDashboard::OnAssetRegistryChanged(const FAssetData& Data)
{
	if (Data.AssetClassPath != UKzDialogueAsset::StaticClass()->GetClassPathName()) { return; }

	// The startup scan storms per-asset events; OnFilesLoaded catches up once at the end.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (Registry.IsLoadingAssets()) { return; }

	// ponytail: full rebuild per registry event; debounce with a timer if a mass import ever stutters.
	RebuildFromRegistry(Data.PackageName);
}

void SKzDialogueDashboard::OnAssetRenamed(const FAssetData& Data, const FString& /*OldPath*/)
{
	OnAssetRegistryChanged(Data);
}

void SKzDialogueDashboard::OnRegistryFilesLoaded()
{
	RebuildFromRegistry();
}

#undef LOCTEXT_NAMESPACE