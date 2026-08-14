// Copyright 2026 kirzo

#include "Localization/SKzDialogueCoveragePanel.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzLibEditorStyle.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetData.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SKzDialogueCoveragePanel"

namespace
{
	const FLinearColor KzDoneColor(0.35f, 0.8f, 0.35f);
	const FLinearColor KzStaleColor(0.9f, 0.75f, 0.3f);
	const FLinearColor KzMissingColor(0.9f, 0.35f, 0.3f);
	const FLinearColor KzPartialColor(0.95f, 0.6f, 0.15f);

	int32 CountWords(const FString& Text)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		return Words.Num();
	}
}

void SKzDialogueCoveragePanel::Construct(const FArguments& InArgs, const TArray<UKzDialogueAsset*>& InAssets)
{
	for (UKzDialogueAsset* Asset : InAssets)
	{
		if (Asset) { Assets.Add(Asset); }
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		// Toolbar band: title left, filters and actions right, on its own panel background.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
				.Padding(FMargin(8.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("Title", "Localization"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildCultureFilterMenu)
							.ToolTipText(LOCTEXT("CultureFilterTip", "Show only one culture's card."))
							.ButtonContent()
							[
								SNew(STextBlock).Text_Lambda([this]() { return CultureFilter.IsEmpty() ? LOCTEXT("AllCultures", "All Cultures") : FKzDialogueTranslationCsv::GetCultureDisplayLabel(CultureFilter); })
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildSpeakerFilterMenu)
							.ToolTipText(LOCTEXT("SpeakerFilterTip", "Count only the lines of one speaker."))
							.ButtonContent()
							[
								SNew(STextBlock).Text_Lambda([this]()
								{
									if (!bSpeakerFilterActive) { return LOCTEXT("AllSpeakers", "All Speakers"); }
									return SpeakerFilter.IsEmpty() ? LOCTEXT("NarrationFilter", "(Narration)") : FText::FromString(SpeakerFilter);
								})
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildFiltersMenu)
							.ToolTipText(LOCTEXT("FiltersTip", "Row filters"))
							.ButtonContent()
							[
								SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.Filter"))
									.ColorAndOpacity(FSlateColor::UseForeground())
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 8.0f, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildViewOptionsMenu)
							.ToolTipText(LOCTEXT("ViewOptionsTip", "View options"))
							.ButtonContent()
							[
								SNew(SImage)
									.Image(FAppStyle::GetBrush("GenericViewButton"))
									.ColorAndOpacity(FSlateColor::UseForeground())
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildExportMenu)
							.ToolTipText(LOCTEXT("ExportTip", "Export the assets' texts for translation. Pick the format (CSV with context columns, or standard PO with context comments) and the scope (all, filtered, pending)."))
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("Export", "Export"))
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildImportMenu)
							.ToolTipText(LOCTEXT("ImportTip", "Import a translated CSV or PO file into a culture's archive."))
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("Import", "Import"))
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
							.Text(LOCTEXT("Refresh", "Refresh"))
							.OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						// Host-specific buttons (Open Dashboard in the asset editor, Gather/Compile in the dashboard).
						InArgs._ToolbarExtension.Widget
					]
				]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 6.0f, 8.0f, 8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(Rows, SVerticalBox)
			]
		]
	];

	// Follow edits to the assets (line text/audio changes) so the tab never shows stale data.
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SKzDialogueCoveragePanel::OnObjectPropertyChanged);

	Refresh();
}

void SKzDialogueCoveragePanel::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (Object && Assets.ContainsByPredicate([Object](const TWeakObjectPtr<UKzDialogueAsset>& Asset) { return Asset.Get() == Object; }))
	{
		Refresh();
	}
}

void SKzDialogueCoveragePanel::Refresh()
{
	Rows->ClearChildren();

	auto AddMessage = [this](const FText& Message)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
		[
			SNew(STextBlock)
				.Text(Message)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	};

	TArray<UKzDialogueAsset*> LiveAssets;
	for (const TWeakObjectPtr<UKzDialogueAsset>& Asset : Assets)
	{
		if (UKzDialogueAsset* AssetPtr = Asset.Get()) { LiveAssets.Add(AssetPtr); }
	}
	if (LiveAssets.IsEmpty())
	{
		AddMessage(LOCTEXT("NoAssets", "No dialogue assets."));
		return;
	}

	FKzLocQuery Query;
	FText Error;
	if (!Query.Load(Error))
	{
		AddMessage(Error);
		return;
	}

	auto PassesSpeaker = [this](const FKzDialogueLine& Line)
	{
		if (!bSpeakerFilterActive) { return true; }
		const FString Name = Line.Speaker.Asset ? Line.Speaker.Asset->GetName() : FString();
		return Name == SpeakerFilter;
	};

	// Native card first: recording state of the source audio (missing takes, takes whose
	// text drifted after recording). Lines that are text-only by design (VoicePolicy, line
	// over project settings) are not recording work; text has nothing to translate.
	if (CultureFilter.IsEmpty() || CultureFilter == Query.GetTarget().NativeCulture)
	{
		const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
		const EKzLineVoicePolicy DefaultVoicePolicy = Settings && Settings->DefaultVoicePolicy != EKzLineVoicePolicy::Inherit
			? Settings->DefaultVoicePolicy : EKzLineVoicePolicy::VoiceExpected;

		int32 AudioDone = 0;
		int32 AudioTotal = 0;
		int32 PendingCount = 0;
		TArray<FAssetLines> Groups;
		for (UKzDialogueAsset* AssetPtr : LiveAssets)
		{
			FAssetLines& Group = Groups.AddDefaulted_GetRef();
			Group.Asset = AssetPtr;

			for (const FKzDialogueLine& Line : AssetPtr->Lines)
			{
				if (!PassesSpeaker(Line)) { continue; }

				const bool bVoiced = !Line.Audio.IsNull();
				const bool bStale = bVoiced && Line.RecordedTextHash != 0 && Line.RecordedTextHash != Line.SourceTextHash;
				const EKzLineVoicePolicy VoicePolicy = Line.VoicePolicy != EKzLineVoicePolicy::Inherit ? Line.VoicePolicy : DefaultVoicePolicy;

				FLineRow Row;
				Row.LineId = Line.LineId;
				Row.Label = Line.GetDisplayLabel(60);
				if (bVoiced)
				{
					Row.AudioPath = Line.Audio.ToSoftObjectPath();
				}
				if (!bVoiced && VoicePolicy == EKzLineVoicePolicy::TextOnly)
				{
					// Deliberately unvoiced: informative row, no work and outside the totals.
					Row.State = LOCTEXT("RowTextOnly", "text only");
					Row.StateColor = FLinearColor(0.5f, 0.5f, 0.5f);
				}
				else if (bVoiced && !bStale)
				{
					++AudioTotal;
					++AudioDone;
					Row.State = LOCTEXT("RowOk", "ok");
					Row.StateColor = KzDoneColor;
				}
				else
				{
					++AudioTotal;
					++PendingCount;
					Row.bPending = true;
					Row.bAudioWork = true;
					Row.bAcknowledgeable = bVoiced;
					Row.State = bVoiced ? LOCTEXT("PendingStaleAudio", "stale audio") : LOCTEXT("PendingNoAudio", "no audio");
					Row.StateColor = bVoiced ? KzStaleColor : KzMissingColor;
				}
				Group.Lines.Add(MoveTemp(Row));
			}
		}

		if (!(bOnlyIncomplete && PendingCount == 0))
		{
			TArray<TSharedRef<SWidget>> Bars;
			Bars.Add(MakeProgressRow(LOCTEXT("BarRecording", "Recording"), AudioDone, AudioTotal, 0));
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				MakeCultureCard(
					FText::Format(LOCTEXT("NativeCardTitle", "{0} - native"), FKzDialogueTranslationCsv::GetCultureDisplayLabel(Query.GetTarget().NativeCulture)),
					MoveTemp(Bars), Groups, TEXT("native"))
			];
		}
	}

	if (Query.GetTarget().ForeignCultures.IsEmpty())
	{
		AddMessage(LOCTEXT("NoCultures", "The localization target has no foreign cultures."));
		return;
	}

	for (const FString& Culture : Query.GetTarget().ForeignCultures)
	{
		if (!CultureFilter.IsEmpty() && Culture != CultureFilter) { continue; }

		int32 TextDone = 0;
		int32 TextTotal = 0;
		int32 TextStale = 0;
		int32 PendingWords = 0;
		int32 AudioDone = 0;
		int32 AudioTotal = 0;
		int32 PendingCount = 0;
		TArray<FAssetLines> Groups;

		for (UKzDialogueAsset* AssetPtr : LiveAssets)
		{
			FAssetLines& Group = Groups.AddDefaulted_GetRef();
			Group.Asset = AssetPtr;

			for (const FKzDialogueLine& Line : AssetPtr->Lines)
			{
				if (!PassesSpeaker(Line)) { continue; }

				bool bTextMissing = false;
				bool bTextStale = false;
				bool bTextTooLong = false;
				bool bAudioMissing = false;
				FText PendingTooltip;
				FSoftObjectPath LocalizedAudioPath;

				// Text state, anchored to the line's stable namespace/key.
				const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
				const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
				const FString* Source = FTextInspector::GetSourceString(Line.Text);
				if (!Line.Text.IsEmpty() && Namespace.IsSet() && Key.IsSet() && Source)
				{
					++TextTotal;

					FString ArchiveSource;
					FString ArchiveTranslation;
					const bool bHasEntry = Query.GetArchiveEntry(Namespace.GetValue(), Key.GetValue(), Culture, ArchiveSource, ArchiveTranslation);

					switch (Query.GetTextState(Namespace.GetValue(), Key.GetValue(), *Source, Culture))
					{
					case EKzTranslationState::Translated:
						++TextDone;
						// Translated but over the line's subtitle budget: surfaced as pending work.
						if (Line.MaxCharacters > 0 && bHasEntry && ArchiveTranslation.Len() > Line.MaxCharacters)
						{
							bTextTooLong = true;
							PendingTooltip = FText::Format(
								LOCTEXT("TooLongTip", "Translation is {0} characters; the line allows {1}:\n{2}"),
								ArchiveTranslation.Len(), Line.MaxCharacters, FText::FromString(ArchiveTranslation));
						}
						break;
					case EKzTranslationState::Stale:
						++TextStale;
						bTextStale = true;
						PendingWords += CountWords(*Source);
						// The archive keeps the source each translation was made against, so the
						// diff shows exactly what changed since then.
						if (bHasEntry)
						{
							PendingTooltip = FText::Format(
								LOCTEXT("StaleDiffTip", "Translated against:\n{0}\n\nCurrent text:\n{1}\n\nCurrent translation:\n{2}"),
								FText::FromString(ArchiveSource), FText::FromString(*Source), FText::FromString(ArchiveTranslation));
						}
						break;
					default:
						bTextMissing = true;
						PendingWords += CountWords(*Source);
						break;
					}
				}

				// Localized audio variant of voiced lines. The eye toggle silences this whole
				// dimension for projects that do not localize audio; bLocalizeAudio opts a
				// single line out (its source take plays in every culture on purpose).
				if (bShowLocalizedAudio && !Line.Audio.IsNull() && Line.bLocalizeAudio)
				{
					++AudioTotal;
					FString LocalizedPackage;
					const FSoftObjectPath SourcePath = Line.Audio.ToSoftObjectPath();
					if (FPackageLocalizationUtil::ConvertSourceToLocalized(SourcePath.GetLongPackageName(), Culture, LocalizedPackage) && FPackageName::DoesPackageExist(LocalizedPackage))
					{
						++AudioDone;
						LocalizedAudioPath = FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *LocalizedPackage, *SourcePath.GetAssetName()));
					}
					else
					{
						bAudioMissing = true;
					}
				}

				// One row per line; the state chip says what exactly is pending, or "ok".
				FLineRow Row;
				Row.LineId = Line.LineId;
				Row.Label = Line.GetDisplayLabel(60);
				Row.Tooltip = PendingTooltip;
				Row.AudioPath = LocalizedAudioPath;
				if (bTextMissing || bTextStale || bTextTooLong || bAudioMissing)
				{
					++PendingCount;
					Row.bPending = true;
					Row.bAudioWork = bAudioMissing;

					const FText TextPart = bTextMissing ? LOCTEXT("PendingMissing", "text")
						: bTextStale ? LOCTEXT("PendingStale", "stale")
						: bTextTooLong ? LOCTEXT("PendingTooLong", "too long")
						: FText::GetEmpty();

					if (!TextPart.IsEmpty() && bAudioMissing)
					{
						Row.State = FText::Format(LOCTEXT("PendingCombo", "{0} + audio"), TextPart);
					}
					else
					{
						Row.State = TextPart.IsEmpty() ? LOCTEXT("PendingAudio", "audio") : TextPart;
					}
					Row.StateColor = (bTextMissing || bAudioMissing) ? KzMissingColor : KzStaleColor;
				}
				else
				{
					Row.State = LOCTEXT("RowOk", "ok");
					Row.StateColor = KzDoneColor;
				}
				Group.Lines.Add(MoveTemp(Row));
			}
		}

		if (bOnlyIncomplete && PendingCount == 0)
		{
			continue;
		}

		TArray<TSharedRef<SWidget>> Bars;
		Bars.Add(MakeProgressRow(LOCTEXT("BarText", "Text"), TextDone, TextTotal, TextStale, PendingWords));
		if (AudioTotal > 0)
		{
			Bars.Add(MakeProgressRow(LOCTEXT("BarAudio", "Audio"), AudioDone, AudioTotal, 0));
		}

		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			MakeCultureCard(FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture), MoveTemp(Bars), Groups, Culture)
		];
	}
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeCultureCard(const FText& Title, TArray<TSharedRef<SWidget>> ProgressRows, const TArray<FAssetLines>& Groups, const FString& AudioKeyPrefix)
{
	// Progress (badge + bars) always reflects every line; the ok-filters below only shape the list.
	int32 PendingCount = 0;
	for (const FAssetLines& Group : Groups)
	{
		for (const FLineRow& Row : Group.Lines)
		{
			if (Row.bPending) { ++PendingCount; }
		}
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
				.Text(Title)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
				.Text(PendingCount == 0 ? LOCTEXT("CardComplete", "complete") : FText::Format(LOCTEXT("CardPendingCount", "{0} pending"), PendingCount))
				.ColorAndOpacity(FSlateColor(PendingCount == 0 ? KzDoneColor : KzPartialColor))
		]
	];

	for (const TSharedRef<SWidget>& Bar : ProgressRows)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[ Bar ];
	}

	// The ok-filters shape which rows show; the counters above ignore them on purpose.
	// Asset header rows only appear when the panel hosts more than one asset.
	const bool bAssetHeaders = Assets.Num() > 1;
	int32 VisibleCount = 0;
	TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);
	for (const FAssetLines& Group : Groups)
	{
		TArray<const FLineRow*> Visible;
		for (const FLineRow& Row : Group.Lines)
		{
			if (bOnlyIncomplete && !Row.bPending) { continue; }
			if (bOnlyMissingVoice && !Row.bAudioWork) { continue; }
			Visible.Add(&Row);
		}
		if (Visible.IsEmpty()) { continue; }

		if (bAssetHeaders)
		{
			ListBox->AddSlot().AutoHeight().Padding(0.0f, VisibleCount > 0 ? 6.0f : 1.0f, 0.0f, 1.0f)
			[
				MakeAssetHeaderRow(Group.Asset)
			];
		}
		for (const FLineRow* RowPtr : Visible)
		{
			ListBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				MakeLineRowWidget(*RowPtr, Group.Asset, AudioKeyPrefix)
			];
		}
		VisibleCount += Visible.Num();
	}

	if (VisibleCount > 0)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(FText::Format(LOCTEXT("LinesArea", "Lines ({0})"), VisibleCount))
				.BodyContent()
				[
					// Recessed inset behind the row cells, so the list reads as its own region.
					SNew(SBorder)
						.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.GroupBorder"))
						.Padding(4.0f)
						[
							ListBox
						]
				]
		];
	}

	return SNew(SBorder)
		.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.CardBorder"))
		.Padding(10.0f)
		[
			Content
		];
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeAssetHeaderRow(TWeakObjectPtr<UKzDialogueAsset> InAsset)
{
	UKzDialogueAsset* AssetPtr = InAsset.Get();
	if (!AssetPtr) { return SNullWidget::NullWidget; }

	const FString Label = AssetPtr->DisplayName.IsEmpty() ? AssetPtr->GetName() : AssetPtr->DisplayName;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(6.0f, 3.0f))
		.ToolTipText(FText::Format(LOCTEXT("AssetHeaderTip", "{0}\nDouble-click to open the asset."), FText::FromString(AssetPtr->GetPathName())))
		.OnMouseDoubleClick(FPointerEventHandler::CreateLambda([InAsset](const FGeometry&, const FPointerEvent&)
		{
			if (UKzDialogueAsset* Asset = InAsset.Get(); Asset && GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
			}
			return FReply::Handled();
		}))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.0f))
					.ToolTipText(LOCTEXT("BrowseToAsset", "Find in Content Browser"))
					.OnClicked_Lambda([InAsset]()
					{
						if (UKzDialogueAsset* Asset = InAsset.Get())
						{
							FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
							ContentBrowser.Get().SyncBrowserToAssets(TArray<FAssetData>{ FAssetData(Asset) }, /*bAllowLockedBrowsers=*/true);
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Search"))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
			]
		];
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeLineRowWidget(const FLineRow& Entry, TWeakObjectPtr<UKzDialogueAsset> InAsset, const FString& AudioKeyPrefix)
{
	const FString PlayKey = AudioKeyPrefix + TEXT("|") + Entry.LineId.ToString(EGuidFormats::Digits);

	TSharedRef<SWidget> PlayWidget = SNullWidget::NullWidget;
	if (Entry.AudioPath.IsValid())
	{
		PlayWidget = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText(LOCTEXT("PlayTakeTip", "Preview this culture's take."))
			.OnClicked_Lambda([this, PlayKey, Path = Entry.AudioPath]()
			{
				const bool bWasThis = PreviewKey == PlayKey && PreviewAudio.IsValid() && PreviewAudio->IsPlaying();
				if (GEditor) { GEditor->ResetPreviewAudioComponent(); }
				PreviewAudio = nullptr;
				PreviewKey.Reset();
				if (!bWasThis && GEditor)
				{
					if (USoundBase* Sound = Cast<USoundBase>(Path.TryLoad()))
					{
						PreviewAudio = GEditor->PlayPreviewSound(Sound);
						PreviewKey = PlayKey;
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
					.Image_Lambda([this, PlayKey]()
					{
						const bool bThis = PreviewKey == PlayKey && PreviewAudio.IsValid() && PreviewAudio->IsPlaying();
						return FAppStyle::GetBrush(bThis ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play");
					})
					.ColorAndOpacity(FSlateColor::UseForeground())
			];
	}

	// The state chip content. Stale takes on the native card append an accept icon INSIDE the
	// chip's fixed-width box (nothing shifts, labels stay column-aligned): the take is declared
	// valid for the current text without re-recording.
	TSharedRef<SWidget> ChipContent = SNew(STextBlock)
		.Text(Entry.State)
		.ColorAndOpacity(FSlateColor(Entry.StateColor))
		.ToolTipText(Entry.Tooltip);
	if (Entry.bAcknowledgeable)
	{
		ChipContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				ChipContent
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(1.0f))
					.ToolTipText(LOCTEXT("AcknowledgeTakeTip", "Accept the current text for this take: the recording is still valid, clear the stale state without re-recording."))
					.OnClicked_Lambda([this, InAsset, LineId = Entry.LineId]() { AcknowledgeRecordedText(InAsset, LineId); return FReply::Handled(); })
					[
						SNew(SBox).WidthOverride(13.0f).HeightOverride(13.0f)
						[
							SNew(SImage).Image(FAppStyle::GetBrush("Icons.SuccessWithColor"))
						]
					]
			];
	}

	return SNew(SBorder)
		.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.ListRowBorder"))
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(112.0f).VAlign(VAlign_Center)
				[
					ChipContent
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(22.0f).HeightOverride(20.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					PlayWidget
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.0f, 1.0f))
					.ToolTipText(Entry.Tooltip.IsEmpty() ? LOCTEXT("PendingEntryTip", "Select this line in the Lines tab.") : Entry.Tooltip)
					.OnClicked_Lambda([this, InAsset, LineId = Entry.LineId]() { NavigateToLine(InAsset, LineId); return FReply::Handled(); })
					[
						SNew(STextBlock)
							.Text(Entry.Label)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			]
		];
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeProgressRow(const FText& Label, int32 Done, int32 Total, int32 StaleCount, int32 PendingWords)
{
	const float Percent = Total > 0 ? static_cast<float>(Done) / static_cast<float>(Total) : 1.0f;
	const bool bComplete = Done >= Total;

	// Word count of untranslated/stale sources: the number a translation quote is based on.
	FText Counter;
	if (StaleCount > 0 && PendingWords > 0)
	{
		Counter = FText::Format(LOCTEXT("BarCounterStaleWords", "{0} / {1}  ({2} stale, {3} words)"), Done, Total, StaleCount, PendingWords);
	}
	else if (StaleCount > 0)
	{
		Counter = FText::Format(LOCTEXT("BarCounterStale", "{0} / {1}  ({2} stale)"), Done, Total, StaleCount);
	}
	else if (PendingWords > 0)
	{
		Counter = FText::Format(LOCTEXT("BarCounterWords", "{0} / {1}  ({2} words)"), Done, Total, PendingWords);
	}
	else
	{
		Counter = FText::Format(LOCTEXT("BarCounter", "{0} / {1}"), Done, Total);
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(70.0f)
			[
				SNew(STextBlock).Text(Label).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox).HeightOverride(12.0f)
			[
				SNew(SProgressBar)
					.Percent(Percent)
					.FillColorAndOpacity(FSlateColor(bComplete ? KzDoneColor : KzPartialColor))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(90.0f).HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text(Counter)
			]
		];
}

void SKzDialogueCoveragePanel::NavigateToLine(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId)
{
	UKzDialogueAsset* AssetPtr = InAsset.Get();
	if (!AssetPtr || !LineId.IsValid() || !GEditor) { return; }

	// Open (or focus) the asset's editor first: from the dashboard it may not be open yet.
	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Subsystem->OpenEditorForAsset(AssetPtr);

	IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(AssetPtr, /*bFocusIfOpen=*/false);
	if (Instance && Instance->GetEditorName() == TEXT("KzArrayAssetEditor"))
	{
		static_cast<FKzArrayAssetEditor*>(Instance)->SelectElementById(LineId);
	}
}

void SKzDialogueCoveragePanel::AcknowledgeRecordedText(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId)
{
	UKzDialogueAsset* Asset = InAsset.Get();
	if (!Asset) { return; }
	const int32 LineIndex = Asset->IndexOfLine(LineId);
	if (LineIndex == INDEX_NONE) { return; }

	const FScopedTransaction Transaction(LOCTEXT("AcknowledgeTakeTrans", "Accept Recorded Audio"));
	Asset->Modify();
	Asset->Lines[LineIndex].RecordedTextHash = Asset->Lines[LineIndex].SourceTextHash;
	Asset->MarkPackageDirty();

	Refresh();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildImportMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	FKzLocTargetInfo Target;
	FText Error;
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoImportCultures", "No localization target cultures found"),
			Error,
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return MenuBuilder.MakeWidget();
	}

	// Same culture list under each format; entries run the matching interactive import.
	auto AddCultureEntries = [this](FMenuBuilder& Sub, const TArray<FString>& Cultures, TFunction<void(const FString&)> Run)
	{
		for (const FString& Culture : Cultures)
		{
			Sub.AddMenuEntry(
				FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
				FText::Format(LOCTEXT("ImportCultureEntryTip", "Import a translated file into the '{0}' archive."), FText::FromString(Culture)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Culture, Run]() { Run(Culture); Refresh(); })));
		}
	};

	MenuBuilder.AddSubMenu(
		LOCTEXT("ImportCsvSub", "Translation CSV"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateLambda([AddCultureEntries, Cultures = Target.ForeignCultures](FMenuBuilder& Sub)
		{
			AddCultureEntries(Sub, Cultures, [](const FString& Culture) { FKzDialogueTranslationCsv::ImportInteractive(Culture); });
		}));

	MenuBuilder.AddSubMenu(
		LOCTEXT("ImportPoSub", "Portable Object (.po)"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateLambda([AddCultureEntries, Cultures = Target.ForeignCultures](FMenuBuilder& Sub)
		{
			AddCultureEntries(Sub, Cultures, [](const FString& Culture) { FKzDialogueTranslationCsv::ImportPoInteractive(Culture); });
		}));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildFiltersMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MissingVoice", "Missing voice"),
		LOCTEXT("MissingVoiceTip", "Show only lines with audio work: missing or stale takes, missing localized variants."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyMissingVoice = !bOnlyMissingVoice; Refresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bOnlyMissingVoice; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OnlyIncomplete", "Only incomplete"),
		LOCTEXT("OnlyIncompleteTip", "Hide the 'ok' lines; cultures with nothing pending lose their whole card."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyIncomplete = !bOnlyIncomplete; Refresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bOnlyIncomplete; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildViewOptionsMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowLocalizedAudio", "Localized audio"),
		LOCTEXT("ShowLocalizedAudioTip", "Show the foreign cultures' localized-audio state. Turn off in projects that do not localize audio; the native recording state (missing and stale takes) always shows."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bShowLocalizedAudio = !bShowLocalizedAudio; Refresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bShowLocalizedAudio; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildExportMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	TArray<UKzDialogueAsset*> LiveAssets;
	TArray<FAssetData> AssetData;
	for (const TWeakObjectPtr<UKzDialogueAsset>& Asset : Assets)
	{
		if (UKzDialogueAsset* AssetPtr = Asset.Get())
		{
			LiveAssets.Add(AssetPtr);
			AssetData.Add(FAssetData(AssetPtr));
		}
	}
	if (LiveAssets.IsEmpty())
	{
		return MenuBuilder.MakeWidget();
	}

	// Self-contained copies of the panel filters, so the export lambdas outlive the menu.
	auto PassesSpeaker = [bActive = bSpeakerFilterActive, Name = SpeakerFilter](const FKzDialogueLine& Line)
	{
		if (!bActive) { return true; }
		return (Line.Speaker.Asset ? Line.Speaker.Asset->GetName() : FString()) == Name;
	};

	// Pending text is evaluated against the filtered culture, or every foreign culture when
	// the filter is off. The native culture has no text to translate.
	TSharedRef<FKzLocQuery> Query = MakeShared<FKzLocQuery>();
	FText QueryError;
	TArray<FString> PendingCultures;
	if (Query->Load(QueryError))
	{
		if (CultureFilter.IsEmpty())
		{
			PendingCultures = Query->GetTarget().ForeignCultures;
		}
		else if (CultureFilter != Query->GetTarget().NativeCulture)
		{
			PendingCultures.Add(CultureFilter);
		}
	}

	auto IsTextPending = [Query, PendingCultures](const FKzDialogueLine& Line)
	{
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
		const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
		const FString* Source = FTextInspector::GetSourceString(Line.Text);
		if (Line.Text.IsEmpty() || !Namespace.IsSet() || !Key.IsSet() || !Source) { return false; }

		for (const FString& Culture : PendingCultures)
		{
			if (Query->GetTextState(Namespace.GetValue(), Key.GetValue(), *Source, Culture) != EKzTranslationState::Translated)
			{
				return true;
			}
		}
		return false;
	};

	int32 AllCount = 0;
	int32 FilteredCount = 0;
	int32 PendingCount = 0;
	for (const UKzDialogueAsset* AssetPtr : LiveAssets)
	{
		AllCount += AssetPtr->Lines.Num();
		for (const FKzDialogueLine& Line : AssetPtr->Lines)
		{
			if (!PassesSpeaker(Line)) { continue; }
			++FilteredCount;
			if (!PendingCultures.IsEmpty() && IsTextPending(Line)) { ++PendingCount; }
		}
	}

	// Both formats share the same scopes; each submenu entry runs the matching interactive flow.
	auto AddScopeEntries = [this, AssetData, PassesSpeaker, IsTextPending, AllCount, FilteredCount, PendingCount](FMenuBuilder& Sub, TFunction<void(TArray<FAssetData>, const FKzExportLineFilter&)> Run)
	{
		Sub.AddMenuEntry(
			FText::Format(LOCTEXT("ExportAllLines", "All lines ({0})"), AllCount),
			LOCTEXT("ExportAllLinesTip", "Export every line of the assets."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([AssetData, Run]() { Run(AssetData, nullptr); })));

		Sub.AddMenuEntry(
			FText::Format(LOCTEXT("ExportFilteredLines", "Filtered lines ({0})"), FilteredCount),
			LOCTEXT("ExportFilteredLinesTip", "Export only the lines passing the speaker filter."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([AssetData, Run, PassesSpeaker]()
				{
					Run(AssetData, [PassesSpeaker](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line); });
				}),
				FCanExecuteAction::CreateLambda([this]() { return bSpeakerFilterActive; })));

		Sub.AddMenuEntry(
			FText::Format(LOCTEXT("ExportPendingLines", "Pending text ({0})"), PendingCount),
			CultureFilter.IsEmpty()
				? LOCTEXT("ExportPendingAnyTip", "Export only the lines whose text is missing or stale in some culture (speaker filter applies).")
				: FText::Format(LOCTEXT("ExportPendingCultureTip", "Export only the lines whose text is missing or stale in {0} (speaker filter applies)."), FKzDialogueTranslationCsv::GetCultureDisplayLabel(CultureFilter)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([AssetData, Run, PassesSpeaker, IsTextPending]()
				{
					Run(AssetData, [PassesSpeaker, IsTextPending](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line) && IsTextPending(Line); });
				}),
				FCanExecuteAction::CreateLambda([PendingCount]() { return PendingCount > 0; })));
	};

	MenuBuilder.AddSubMenu(
		LOCTEXT("ExportCsvSub", "Translation CSV"),
		LOCTEXT("ExportCsvSubTip", "One CSV with the source texts and context columns (speaker, notes, max characters, audio, drift hash)."),
		FNewMenuDelegate::CreateLambda([AddScopeEntries](FMenuBuilder& Sub)
		{
			AddScopeEntries(Sub, [](TArray<FAssetData> Data, const FKzExportLineFilter& Filter) { FKzDialogueTranslationCsv::ExportInteractive(MoveTemp(Data), Filter); });
		}));

	MenuBuilder.AddSubMenu(
		LOCTEXT("ExportPoSub", "Portable Object (.po)"),
		LOCTEXT("ExportPoSubTip", "One .po per culture (the standard translation-tool format): context travels as comments, existing translations fill msgstr, stale ones are flagged fuzzy."),
		FNewMenuDelegate::CreateLambda([AddScopeEntries](FMenuBuilder& Sub)
		{
			AddScopeEntries(Sub, [](TArray<FAssetData> Data, const FKzExportLineFilter& Filter) { FKzDialogueTranslationCsv::ExportPoInteractive(MoveTemp(Data), Filter); });
		}));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildCultureFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AllCultures", "All Cultures"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { CultureFilter.Reset(); Refresh(); })));

	FKzLocTargetInfo Target;
	FText Error;
	if (FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error))
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("NativeFilterEntry", "{0} - native"), FKzDialogueTranslationCsv::GetCultureDisplayLabel(Target.NativeCulture)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Culture = Target.NativeCulture]() { CultureFilter = Culture; Refresh(); })));

		for (const FString& Culture : Target.ForeignCultures)
		{
			MenuBuilder.AddMenuEntry(
				FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Culture]() { CultureFilter = Culture; Refresh(); })));
		}
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildSpeakerFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AllSpeakers", "All Speakers"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { bSpeakerFilterActive = false; SpeakerFilter.Reset(); Refresh(); })));

	// Only the speakers these assets actually use; narration listed when present.
	bool bAnyNarration = false;
	TArray<FString> Names;
	for (const TWeakObjectPtr<UKzDialogueAsset>& Asset : Assets)
	{
		const UKzDialogueAsset* AssetPtr = Asset.Get();
		if (!AssetPtr) { continue; }
		for (const FKzDialogueLine& Line : AssetPtr->Lines)
		{
			if (Line.Speaker.Asset) { Names.AddUnique(Line.Speaker.Asset->GetName()); }
			else { bAnyNarration = true; }
		}
	}
	Names.Sort();

	if (bAnyNarration)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NarrationFilter", "(Narration)"),
			LOCTEXT("NarrationFilterTip", "Lines with no speaker."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]() { bSpeakerFilterActive = true; SpeakerFilter.Reset(); Refresh(); })));
	}

	for (const FString& Name : Names)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Name),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Name]() { bSpeakerFilterActive = true; SpeakerFilter = Name; Refresh(); })));
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE