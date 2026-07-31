// Copyright 2026 kirzo

#include "Localization/SKzDialogueCoveragePanel.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzLibEditorStyle.h"
#include "Dashboard/SKzDialogueDashboard.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzDialogueTranslationCsv.h"

#include "Framework/Docking/TabManager.h"

#include "AssetRegistry/AssetData.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Misc/PackageName.h"
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
#include "Framework/MultiBox/MultiBoxBuilder.h"

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

void SKzDialogueCoveragePanel::Construct(const FArguments& /*InArgs*/, UKzDialogueAsset* InAsset)
{
	Asset = InAsset;

	ChildSlot
	[
		SNew(SVerticalBox)
		// Toolbar band: title left, actions right, on its own panel background.
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
								SNew(STextBlock).Text_Lambda([this]() { return CultureFilter.IsEmpty() ? LOCTEXT("AllCultures", "All Cultures") : FText::FromString(CultureFilter); })
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
							.ToolTipText(LOCTEXT("ExportCsvTip", "Export this asset's texts to a translation CSV: every line, the filtered lines or only the pending text."))
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("ExportCsv", "Export CSV"))
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
							.OnGetMenuContent(this, &SKzDialogueCoveragePanel::BuildImportMenu)
							.ToolTipText(LOCTEXT("ImportCsvTip", "Import a translated CSV into a culture's archive."))
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("ImportCsv", "Import CSV"))
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
						SNew(SButton)
							.Text(LOCTEXT("OpenDashboard", "Open Dashboard"))
							.ToolTipText(LOCTEXT("OpenDashboardTip", "Open the project-wide Dialogue Dashboard."))
							.OnClicked_Lambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(SKzDialogueDashboard::TabId);
								return FReply::Handled();
							})
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

	// Follow edits to the asset (line text/audio changes) so the tab never shows stale data.
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SKzDialogueCoveragePanel::OnObjectPropertyChanged);

	Refresh();
}

void SKzDialogueCoveragePanel::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (Object && Object == Asset.Get())
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

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!AssetPtr)
	{
		AddMessage(LOCTEXT("NoAsset", "No asset."));
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
	// text drifted after recording). Every line counts; text has nothing to translate.
	// It is pure audio work, so the audio toggle hides it entirely.
	if (bShowAudio && (CultureFilter.IsEmpty() || CultureFilter == Query.GetTarget().NativeCulture))
	{
		int32 AudioDone = 0;
		int32 AudioTotal = 0;
		TArray<FPending> Pending;
		TArray<FAudioRow> AudioRows;
		for (const FKzDialogueLine& Line : AssetPtr->Lines)
		{
			if (!PassesSpeaker(Line)) { continue; }
			++AudioTotal;

			const bool bVoiced = !Line.Audio.IsNull();
			const bool bStale = bVoiced && Line.RecordedTextHash != 0 && Line.RecordedTextHash != Line.SourceTextHash;
			if (bVoiced)
			{
				AudioRows.Add({ Line.LineId, Line.GetDisplayLabel(60), Line.Audio.ToSoftObjectPath() });
			}
			if (bVoiced && !bStale)
			{
				++AudioDone;
			}
			else
			{
				Pending.Add({ Line.LineId, Line.GetDisplayLabel(60),
					bVoiced ? LOCTEXT("PendingStaleAudio", "stale audio") : LOCTEXT("PendingNoAudio", "no audio"),
					bVoiced ? KzStaleColor : KzMissingColor,
					FText::GetEmpty(), /*bAudioWork=*/true });
			}
		}

		if (!(bOnlyIncomplete && Pending.IsEmpty()))
		{
			TArray<TSharedRef<SWidget>> Bars;
			Bars.Add(MakeProgressRow(LOCTEXT("BarRecording", "Recording"), AudioDone, AudioTotal, 0));
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				MakeCultureCard(
					FText::Format(LOCTEXT("NativeCardTitle", "{0} (native)"), FText::FromString(Query.GetTarget().NativeCulture)),
					MoveTemp(Bars), Pending, AudioRows, TEXT("native"))
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
		TArray<FPending> Pending;
		TArray<FAudioRow> AudioRows;

		for (const FKzDialogueLine& Line : AssetPtr->Lines)
		{
			if (!PassesSpeaker(Line)) { continue; }

			bool bTextMissing = false;
			bool bTextStale = false;
			bool bTextTooLong = false;
			bool bAudioMissing = false;
			FText PendingTooltip;

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

			// Localized audio variant of voiced lines.
			if (bShowAudio && !Line.Audio.IsNull())
			{
				++AudioTotal;
				FString LocalizedPackage;
				const FSoftObjectPath SourcePath = Line.Audio.ToSoftObjectPath();
				if (FPackageLocalizationUtil::ConvertSourceToLocalized(SourcePath.GetLongPackageName(), Culture, LocalizedPackage) && FPackageName::DoesPackageExist(LocalizedPackage))
				{
					++AudioDone;
					AudioRows.Add({ Line.LineId, Line.GetDisplayLabel(60), FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *LocalizedPackage, *SourcePath.GetAssetName())) });
				}
				else
				{
					bAudioMissing = true;
				}
			}

			// One entry per line; the state column says what exactly is pending.
			if (bTextMissing || bTextStale || bTextTooLong || bAudioMissing)
			{
				const FText TextPart = bTextMissing ? LOCTEXT("PendingMissing", "text")
					: bTextStale ? LOCTEXT("PendingStale", "stale")
					: bTextTooLong ? LOCTEXT("PendingTooLong", "too long")
					: FText::GetEmpty();

				FText State;
				if (!TextPart.IsEmpty() && bAudioMissing)
				{
					State = FText::Format(LOCTEXT("PendingCombo", "{0} + audio"), TextPart);
				}
				else
				{
					State = TextPart.IsEmpty() ? LOCTEXT("PendingAudio", "audio") : TextPart;
				}

				const FLinearColor StateColor = (bTextMissing || bAudioMissing) ? KzMissingColor : KzStaleColor;
				Pending.Add({ Line.LineId, Line.GetDisplayLabel(60), State, StateColor, PendingTooltip, bAudioMissing });
			}
		}

		if (bOnlyMissingVoice)
		{
			Pending.RemoveAll([](const FPending& Entry) { return !Entry.bAudioWork; });
		}
		if (bOnlyIncomplete && Pending.IsEmpty())
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
			MakeCultureCard(FText::FromString(Culture), MoveTemp(Bars), Pending, AudioRows, Culture)
		];
	}
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeCultureCard(const FText& Title, TArray<TSharedRef<SWidget>> ProgressRows, const TArray<FPending>& Pending, const TArray<FAudioRow>& AudioRows, const FString& AudioKeyPrefix)
{
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
				.Text(Pending.IsEmpty() ? LOCTEXT("CardComplete", "complete") : FText::Format(LOCTEXT("CardPendingCount", "{0} pending"), Pending.Num()))
				.ColorAndOpacity(FSlateColor(Pending.IsEmpty() ? KzDoneColor : KzPartialColor))
		]
	];

	for (const TSharedRef<SWidget>& Bar : ProgressRows)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[ Bar ];
	}

	if (!Pending.IsEmpty())
	{
		// Each entry is its own list-row cell (same look as the KzLib stack rows) so long
		// lists read as separate items instead of a wall of text on a shared background.
		TSharedRef<SVerticalBox> PendingBox = SNew(SVerticalBox);
		for (const FPending& Entry : Pending)
		{
			PendingBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SBorder)
					.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.ListRowBorder"))
					.Padding(FMargin(4.0f, 2.0f))
					[
						SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(2.0f, 1.0f))
							.ToolTipText(Entry.Tooltip.IsEmpty() ? LOCTEXT("PendingEntryTip", "Select this line in the Lines tab.") : Entry.Tooltip)
							.OnClicked_Lambda([this, LineId = Entry.LineId]() { NavigateToLine(LineId); return FReply::Handled(); })
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(SBox).WidthOverride(112.0f)
									[
										SNew(STextBlock)
											.Text(Entry.State)
											.ColorAndOpacity(FSlateColor(Entry.StateColor))
									]
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(Entry.Label)
										.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								]
							]
					]
			];
		}

		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(FText::Format(LOCTEXT("PendingArea", "Pending ({0})"), Pending.Num()))
				.BodyContent()
				[
					// Recessed inset behind the row cells, so the list reads as its own region.
					SNew(SBorder)
						.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.GroupBorder"))
						.Padding(4.0f)
						[
							PendingBox
						]
				]
		];
	}

	if (!AudioRows.IsEmpty())
	{
		// Auditionable takes for this card's culture: the localized variants on foreign
		// cards, the source takes on the native one. QA without switching editor language.
		TSharedRef<SVerticalBox> AudioBox = SNew(SVerticalBox);
		for (const FAudioRow& Entry : AudioRows)
		{
			const FString PlayKey = AudioKeyPrefix + TEXT("|") + Entry.LineId.ToString(EGuidFormats::Digits);

			AudioBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SBorder)
					.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.ListRowBorder"))
					.Padding(FMargin(4.0f, 2.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SButton)
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
									SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
									[
										SNew(SImage)
											.Image_Lambda([this, PlayKey]()
											{
												const bool bThis = PreviewKey == PlayKey && PreviewAudio.IsValid() && PreviewAudio->IsPlaying();
												return FAppStyle::GetBrush(bThis ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play");
											})
											.ColorAndOpacity(FSlateColor::UseForeground())
									]
								]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ContentPadding(FMargin(2.0f, 1.0f))
								.ToolTipText(LOCTEXT("PendingEntryTip", "Select this line in the Lines tab."))
								.OnClicked_Lambda([this, LineId = Entry.LineId]() { NavigateToLine(LineId); return FReply::Handled(); })
								[
									SNew(STextBlock)
										.Text(Entry.Label)
										.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								]
						]
					]
			];
		}

		Content->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(FText::Format(LOCTEXT("AudioArea", "Audio ({0})"), AudioRows.Num()))
				.BodyContent()
				[
					SNew(SBorder)
						.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.GroupBorder"))
						.Padding(4.0f)
						[
							AudioBox
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

void SKzDialogueCoveragePanel::NavigateToLine(FGuid LineId)
{
	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!AssetPtr || !LineId.IsValid() || !GEditor) { return; }

	IAssetEditorInstance* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(AssetPtr, /*bFocusIfOpen=*/false);
	if (Instance && Instance->GetEditorName() == TEXT("KzArrayAssetEditor"))
	{
		static_cast<FKzArrayAssetEditor*>(Instance)->SelectElementById(LineId);
	}
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

	for (const FString& Culture : Target.ForeignCultures)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Culture),
			FText::Format(LOCTEXT("ImportCultureTip", "Import a translated CSV into the '{0}' archive."), FText::FromString(Culture)),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Culture]()
			{
				FKzDialogueTranslationCsv::ImportInteractive(Culture);
				Refresh();
			})));
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildFiltersMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MissingVoice", "Missing voice"),
		LOCTEXT("MissingVoiceTip", "Show only pending entries with audio work: missing or stale takes, missing localized variants."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyMissingVoice = !bOnlyMissingVoice; Refresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bOnlyMissingVoice; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OnlyIncomplete", "Only incomplete"),
		LOCTEXT("OnlyIncompleteTip", "Hide the cards of cultures with nothing pending."),
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
		LOCTEXT("ShowAudio", "Audio"),
		LOCTEXT("ShowAudioTip", "Include audio work: recording state and localized variants. Off shows text only."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bShowAudio = !bShowAudio; Refresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bShowAudio; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildExportMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!AssetPtr)
	{
		return MenuBuilder.MakeWidget();
	}
	const FAssetData AssetData(AssetPtr);

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

	int32 FilteredCount = 0;
	int32 PendingCount = 0;
	for (const FKzDialogueLine& Line : AssetPtr->Lines)
	{
		if (!PassesSpeaker(Line)) { continue; }
		++FilteredCount;
		if (!PendingCultures.IsEmpty() && IsTextPending(Line)) { ++PendingCount; }
	}

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportAllLines", "All lines ({0})"), AssetPtr->Lines.Num()),
		LOCTEXT("ExportAllLinesTip", "Export every line of the asset."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([AssetData]()
		{
			FKzDialogueTranslationCsv::ExportInteractive({ AssetData });
		})));

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportFilteredLines", "Filtered lines ({0})"), FilteredCount),
		LOCTEXT("ExportFilteredLinesTip", "Export only the lines passing the speaker filter."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([AssetData, PassesSpeaker]()
			{
				FKzDialogueTranslationCsv::ExportInteractive({ AssetData },
					[PassesSpeaker](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line); });
			}),
			FCanExecuteAction::CreateLambda([this]() { return bSpeakerFilterActive; })));

	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("ExportPendingLines", "Pending text ({0})"), PendingCount),
		CultureFilter.IsEmpty()
			? LOCTEXT("ExportPendingAnyTip", "Export only the lines whose text is missing or stale in some culture (speaker filter applies).")
			: FText::Format(LOCTEXT("ExportPendingCultureTip", "Export only the lines whose text is missing or stale in '{0}' (speaker filter applies)."), FText::FromString(CultureFilter)),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([AssetData, PassesSpeaker, IsTextPending]()
			{
				FKzDialogueTranslationCsv::ExportInteractive({ AssetData },
					[PassesSpeaker, IsTextPending](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line) && IsTextPending(Line); });
			}),
			FCanExecuteAction::CreateLambda([PendingCount]() { return PendingCount > 0; })));

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
			FText::Format(LOCTEXT("NativeFilterEntry", "{0} (native)"), FText::FromString(Target.NativeCulture)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Culture = Target.NativeCulture]() { CultureFilter = Culture; Refresh(); })));

		for (const FString& Culture : Target.ForeignCultures)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(Culture),
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

	// Only the speakers this asset actually uses; narration listed when present.
	bool bAnyNarration = false;
	TArray<FString> Names;
	if (const UKzDialogueAsset* AssetPtr = Asset.Get())
	{
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

void SKzDialogueCoveragePanel::FillCoverageRows(SVerticalBox& Rows, const TArray<FKzCultureCoverage>& Cultures)
{
	auto AddRow = [&Rows](const FText& Culture, const FText& Translated, const FText& Missing, const FText& StaleCount, const FText& Audio, bool bHeader)
	{
		const FSlateFontInfo Font = FAppStyle::GetFontStyle(bHeader ? "BoldFont" : "NormalFont");
		auto Cell = [&Font](const FText& Value) { return SNew(STextBlock).Text(Value).Font(Font); };

		Rows.AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.2f)[ Cell(Culture) ]
			+ SHorizontalBox::Slot().FillWidth(0.25f)[ Cell(Translated) ]
			+ SHorizontalBox::Slot().FillWidth(0.2f)[ Cell(Missing) ]
			+ SHorizontalBox::Slot().FillWidth(0.15f)[ Cell(StaleCount) ]
			+ SHorizontalBox::Slot().FillWidth(0.2f)[ Cell(Audio) ]
		];
	};

	AddRow(LOCTEXT("ColCulture", "Culture"), LOCTEXT("ColTranslated", "Translated"), LOCTEXT("ColMissing", "Missing"), LOCTEXT("ColStale", "Stale"), LOCTEXT("ColAudio", "Audio"), true);

	for (const FKzCultureCoverage& Coverage : Cultures)
	{
		AddRow(
			FText::FromString(Coverage.Culture),
			FText::Format(LOCTEXT("TranslatedCell", "{0} / {1}"), Coverage.Translated, Coverage.Total),
			FText::AsNumber(Coverage.Missing),
			FText::AsNumber(Coverage.Stale),
			Coverage.VoicedLines > 0 ? FText::Format(LOCTEXT("AudioCell", "{0} / {1}"), Coverage.LocalizedAudio, Coverage.VoicedLines) : LOCTEXT("AudioNone", "-"),
			false);
	}
}

#undef LOCTEXT_NAMESPACE