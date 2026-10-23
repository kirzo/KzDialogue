// Copyright 2026 kirzo

#include "Localization/SKzDialogueCoveragePanel.h"
#include "KzDialogueAudition.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzLibEditorStyle.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "LocalizationCommandletTasks.h"
#include "LocalizationModule.h"
#include "LocalizationTargetTypes.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
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
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
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

	/** Row labels are single-line: the full text flows to the divider and the widget's ellipsis cuts it there, so multi-line sources flatten to spaces. */
	FString FlattenLabel(FString Value)
	{
		Value.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		return Value;
	}

	/** The archive keeps the source each translation was made against, so the diff shows exactly what changed since. */
	FText MakeStaleDiffTip(const FString& TranslatedAgainst, const FString& Current, const FString& Translation)
	{
		return FText::Format(LOCTEXT("StaleDiffTip", "Translated against:\n{0}\n\nCurrent text:\n{1}\n\nCurrent translation:\n{2}"),
			FText::FromString(TranslatedAgainst), FText::FromString(Current), FText::FromString(Translation));
	}

	/** An area body built the first time it is shown, so a collapsed area costs nothing. */
	struct FKzLazyBody
	{
		TSharedRef<SBox> Box = SNew(SBox);
		TFunction<TSharedRef<SWidget>()> Build;

		void Ensure()
		{
			if (!Build) { return; }
			Box->SetContent(Build());
			Build.Reset();
		}
	};

	TSharedRef<FKzLazyBody> MakeLazyBody(TFunction<TSharedRef<SWidget>()> Build, bool bExpanded)
	{
		const TSharedRef<FKzLazyBody> Body = MakeShared<FKzLazyBody>();
		Body->Build = MoveTemp(Build);
		if (bExpanded) { Body->Ensure(); }
		return Body;
	}
}

void SKzDialogueCoveragePanel::Construct(const FArguments& InArgs, const TArray<UKzDialogueAsset*>& InAssets)
{
	bIncludeProjectTexts = InArgs._bIncludeProjectTexts;
	LoadPersistedFilters();

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
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SBox)
							.WidthOverride(200.0f)
							[
								SNew(SSearchBox)
									.HintText(LOCTEXT("SearchHint", "Search text..."))
									.ToolTipText(LOCTEXT("SearchTip", "Show only the rows whose source text or translation contains this text."))
									.DelayChangeNotificationsWhileTyping(true)
									.OnTextChanged_Lambda([this](const FText& NewText) { TextFilter = NewText.ToString(); RequestRefresh(); })
							]
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
							.ToolTipText(LOCTEXT("RefreshTip", "Re-read the assets and archives. Also forgets the optimistically hidden merged/non-localizable rows (e.g. after undoing one of those actions)."))
							.OnClicked_Lambda([this]() { HandledIdentities.Reset(); CachedQuery.Reset(); RequestRefresh(); return FReply::Handled(); })
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
	FKzArchiveWriter::OnArchivesChanged.AddSP(this, &SKzDialogueCoveragePanel::OnArchivesChanged);

	Refresh();
}

void SKzDialogueCoveragePanel::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (Object && Assets.ContainsByPredicate([Object](const TWeakObjectPtr<UKzDialogueAsset>& Asset) { return Asset.Get() == Object; }))
	{
		RequestRefresh();
	}
}

void SKzDialogueCoveragePanel::OnArchivesChanged()
{
	CachedQuery.Reset();
	RequestRefresh();
}

void SKzDialogueCoveragePanel::RequestRefresh(TArray<FString> FocusAfter)
{
	if (!FocusAfter.IsEmpty()) { PendingFocus = MoveTemp(FocusAfter); }
	if (bRefreshPending) { return; }
	bRefreshPending = true;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis = TWeakPtr<SKzDialogueCoveragePanel>(SharedThis(this))](float)
	{
		if (TSharedPtr<SKzDialogueCoveragePanel> Panel = WeakThis.Pin())
		{
			if (Panel->bRefreshPending) { Panel->Refresh(); }
			if (!Panel->PendingFocus.IsEmpty())
			{
				Panel->FocusTranslationEditor(Panel->PendingFocus);
				Panel->PendingFocus.Reset();
			}
		}
		return false;
	}));
}

void SKzDialogueCoveragePanel::LoadPersistedFilters()
{
	// GetBool leaves the member untouched when the key is missing, so code defaults hold.
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bShowLocalizedAudio"), bShowLocalizedAudio, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bOnlyMissingVoice"), bOnlyMissingVoice, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bOnlyIncomplete"), bOnlyIncomplete, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bShowDialogueLines"), bShowDialogueLines, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bShowOtherTexts"), bShowOtherTexts, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bOnlyMergeableTexts"), bOnlyMergeableTexts, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("KzDialogueCoveragePanel"), TEXT("bExportProjectTexts"), bExportProjectTexts, GEditorPerProjectIni);
}

void SKzDialogueCoveragePanel::SavePersistedFilter(const TCHAR* Key, bool bValue) const
{
	GConfig->SetBool(TEXT("KzDialogueCoveragePanel"), Key, bValue, GEditorPerProjectIni);
	// Straight to disk: a raw GConfig write is not guaranteed to survive shutdown otherwise.
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SKzDialogueCoveragePanel::Refresh()
{
	bRefreshPending = false;
	FText Message;
	TArray<FCultureCard> Cards = BuildCards(Message);
	Render(MoveTemp(Cards), Message);
}

void SKzDialogueCoveragePanel::Render(TArray<FCultureCard> Cards, const FText& Message)
{
	Rows->ClearChildren();
	TranslationEditors.Reset();

	for (FCultureCard& Card : Cards)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			MakeCultureCard(MoveTemp(Card))
		];
	}

	if (!Message.IsEmpty())
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
		[
			SNew(STextBlock)
				.Text(Message)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

void SKzDialogueCoveragePanel::FillTextState(FLineRow& Row, EKzTranslationState State)
{
	switch (State)
	{
	case EKzTranslationState::Translated:
		Row.State = LOCTEXT("RowOk", "ok");
		Row.StateColor = KzDoneColor;
		break;
	case EKzTranslationState::Stale:
		Row.bPending = true;
		Row.State = LOCTEXT("PendingStale", "stale");
		Row.StateColor = KzStaleColor;
		break;
	default:
		Row.bPending = true;
		Row.State = LOCTEXT("PendingMissing", "text");
		Row.StateColor = KzMissingColor;
		break;
	}
}

TArray<SKzDialogueCoveragePanel::FCultureCard> SKzDialogueCoveragePanel::BuildCards(FText& OutMessage)
{
	TArray<FCultureCard> Cards;

	TArray<UKzDialogueAsset*> LiveAssets;
	for (const TWeakObjectPtr<UKzDialogueAsset>& Asset : Assets)
	{
		if (UKzDialogueAsset* AssetPtr = Asset.Get()) { LiveAssets.Add(AssetPtr); }
	}
	if (LiveAssets.IsEmpty())
	{
		OutMessage = LOCTEXT("NoAssets", "No dialogue assets.");
		return Cards;
	}

	if (!CachedQuery.IsValid())
	{
		const TSharedRef<FKzLocQuery> Loaded = MakeShared<FKzLocQuery>();
		FText Error;
		if (!Loaded->Load(Error))
		{
			OutMessage = Error;
			return Cards;
		}
		CachedQuery = Loaded;
	}
	const FKzLocQuery& Query = *CachedQuery;

	auto PassesSpeaker = [this](const FKzDialogueLine& Line)
	{
		if (!bSpeakerFilterActive) { return true; }
		const FString Name = Line.Speaker.Asset ? Line.Speaker.Asset->GetName() : FString();
		return Name == SpeakerFilter;
	};

	// The search box narrows every card by source text, counters included (like the speaker
	// filter, unlike the ok-filters).
	auto PassesText = [this](const FKzDialogueLine& Line)
	{
		if (TextFilter.IsEmpty()) { return true; }
		const FString* Source = FTextInspector::GetSourceString(Line.Text);
		return Source && Source->Contains(TextFilter, ESearchCase::IgnoreCase);
	};

	// Native card first: recording state of the source audio (missing takes, takes whose
	// text drifted after recording). Lines that are text-only by design (VoicePolicy, line
	// over project settings) are not recording work; text has nothing to translate.
	if (bShowDialogueLines && (CultureFilter.IsEmpty() || CultureFilter == Query.GetTarget().NativeCulture))
	{
		const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
		const EKzLineVoicePolicy DefaultVoicePolicy = Settings && Settings->DefaultVoicePolicy != EKzLineVoicePolicy::Inherit
			? Settings->DefaultVoicePolicy : EKzLineVoicePolicy::VoiceExpected;

		// Identical-source localizable lines across every loaded asset: candidates for the
		// line-text merge (one shared localization identity; audio and timing stay per line).
		TMap<FString, TArray<TPair<TWeakObjectPtr<UKzDialogueAsset>, FGuid>>> LinesBySource;
		for (UKzDialogueAsset* AssetPtr : LiveAssets)
		{
			for (const FKzDialogueLine& Line : AssetPtr->Lines)
			{
				if (Line.Text.IsCultureInvariant()) { continue; }
				const FString* Source = FTextInspector::GetSourceString(Line.Text);
				if (!Source || Source->IsEmpty()) { continue; }
				LinesBySource.FindOrAdd(*Source).Emplace(AssetPtr, Line.LineId);
			}
		}

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
				if (!PassesSpeaker(Line) || !PassesText(Line)) { continue; }

				const bool bVoiced = !Line.Audio.IsNull();
				const bool bStale = bVoiced && Line.RecordedTextHash != 0 && Line.RecordedTextHash != Line.SourceTextHash;
				const EKzLineVoicePolicy VoicePolicy = Line.VoicePolicy != EKzLineVoicePolicy::Inherit ? Line.VoicePolicy : DefaultVoicePolicy;

				FLineRow Row;
				Row.LineId = Line.LineId;
				Row.Label = FText::FromString(FlattenLabel(Line.GetDisplayLabel(0).ToString()));
				Row.bSharedText = Line.SharedTextId.IsValid();
				if (const FString* Source = FTextInspector::GetSourceString(Line.Text))
				{
					if (const TArray<TPair<TWeakObjectPtr<UKzDialogueAsset>, FGuid>>* SameSource = LinesBySource.Find(*Source))
					{
						if (SameSource->Num() > 1) { Row.LineMergeGroup = *SameSource; }
					}
				}
				if (Row.bSharedText)
				{
					Row.Label = FText::Format(LOCTEXT("SharedLineLabel", "{0} (shared)"), Row.Label);
				}
				else if (Row.LineMergeGroup.Num() > 1)
				{
					Row.Label = FText::Format(LOCTEXT("MergeableLineLabel", "{0} (x{1})"), Row.Label, FText::AsNumber(Row.LineMergeGroup.Num()));
				}
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
			FCultureCard& Card = Cards.AddDefaulted_GetRef();
			Card.Title = FText::Format(LOCTEXT("NativeCardTitle", "{0} - native"), FKzDialogueTranslationCsv::GetCultureDisplayLabel(Query.GetTarget().NativeCulture));
			Card.Culture = Query.GetTarget().NativeCulture;
			Card.KeyPrefix = TEXT("native");
			Card.Bars.Add({ LOCTEXT("BarRecording", "Recording"), AudioDone, AudioTotal });
			Card.Groups = MoveTemp(Groups);
		}
	}

	if (Query.GetTarget().ForeignCultures.IsEmpty())
	{
		OutMessage = LOCTEXT("NoCultures", "The localization target has no foreign cultures.");
		return Cards;
	}

	// Speaker name fields and shared words, the same set the exports ship. Project scope adds
	// every speaker and word; the speaker filter narrows both to that speaker's.
	TArray<FKzLocText> OwnedTexts;
	{
		FKzExportLineFilter SpeakerLines;
		if (bSpeakerFilterActive) { SpeakerLines = [&PassesSpeaker](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line); }; }
		const bool bProjectWide = bIncludeProjectTexts && !bSpeakerFilterActive;
		FKzDialogueTranslationCsv::CollectLocTexts(LiveAssets, SpeakerLines, bProjectWide, bProjectWide, OwnedTexts);
		OwnedTexts.RemoveAll([](const FKzLocText& Text) { return Text.Line != nullptr; });
	}

	// Non-dialogue gathered texts (UI, menus...), collected once and evaluated per culture.
	struct FOtherText
	{
		FString Namespace;
		FString Key;
		FString Source;
	};
	TArray<FOtherText> OtherTexts;
	if (bIncludeProjectTexts && bShowOtherTexts)
	{
		Query.EnumerateOtherTexts([this, &OtherTexts](const FString& Namespace, const FString& Key, const FString& Source)
		{
			// Skip identities already handled from this panel; the manifest keeps them until the next Gather.
			if (!HandledIdentities.Contains(Namespace + TEXT(",") + Key))
			{
				OtherTexts.Add({ Namespace, Key, Source });
			}
		});
	}

	// With the lines eye off and nothing else to show, the foreign cards have no content.
	if (!bShowDialogueLines && OtherTexts.IsEmpty())
	{
		return Cards;
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
			if (!bShowDialogueLines) { break; }

			FAssetLines& Group = Groups.AddDefaulted_GetRef();
			Group.Asset = AssetPtr;

			for (const FKzDialogueLine& Line : AssetPtr->Lines)
			{
				if (!PassesSpeaker(Line)) { continue; }

				// Text identity and this culture's archive entry, fetched up front: the search
				// filter matches the source text OR the translation shown on this card.
				const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
				const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
				const FString* Source = FTextInspector::GetSourceString(Line.Text);
				const bool bLocalizable = Namespace.IsSet() && Key.IsSet() && Source && !Source->IsEmpty();

				FString ArchiveSource;
				FString ArchiveTranslation;
				const bool bHasEntry = bLocalizable && Query.GetArchiveEntry(Namespace.GetValue(), Key.GetValue(), Culture, ArchiveSource, ArchiveTranslation);

				if (!TextFilter.IsEmpty() && !PassesText(Line) && !(bHasEntry && ArchiveTranslation.Contains(TextFilter, ESearchCase::IgnoreCase))) { continue; }

				// A non-localizable line has no text work: its chip only reflects the audio.
				EKzTranslationState TextState = EKzTranslationState::Translated;
				bool bTextTooLong = false;
				bool bAudioMissing = false;
				FText PendingTooltip;
				FSoftObjectPath LocalizedAudioPath;

				if (bLocalizable)
				{
					++TextTotal;
					TextState = Query.GetTextState(Namespace.GetValue(), Key.GetValue(), *Source, Culture);
					switch (TextState)
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
						PendingWords += CountWords(*Source);
						if (bHasEntry) { PendingTooltip = MakeStaleDiffTip(ArchiveSource, *Source, ArchiveTranslation); }
						break;
					default:
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

				FLineRow Row;
				Row.LineId = Line.LineId;
				Row.Label = FText::FromString(FlattenLabel(Line.GetDisplayLabel(0).ToString()));
				Row.Tooltip = PendingTooltip;
				Row.Translation = FText::FromString(bHasEntry ? ArchiveTranslation : FString());
				if (Namespace.IsSet() && Key.IsSet() && Source)
				{
					Row.TranslationCulture = Culture;
					Row.TranslationIdentities.Emplace(Namespace.GetValue(), Key.GetValue());
					Row.TranslationSource = *Source;
				}
				Row.AudioPath = LocalizedAudioPath;
				Row.bCanMakeNonLocalizable = !Line.Text.IsEmpty() && Namespace.IsSet() && Key.IsSet();

				// One chip per line saying what exactly is pending: the text part, then the audio.
				FillTextState(Row, TextState);
				if (bTextTooLong)
				{
					Row.bPending = true;
					Row.State = LOCTEXT("PendingTooLong", "too long");
					Row.StateColor = KzStaleColor;
				}
				if (bAudioMissing)
				{
					Row.State = Row.bPending ? FText::Format(LOCTEXT("PendingCombo", "{0} + audio"), Row.State) : LOCTEXT("PendingAudio", "audio");
					Row.bPending = true;
					Row.bAudioWork = true;
					Row.StateColor = KzMissingColor;
				}
				if (Row.bPending) { ++PendingCount; }
				Group.Lines.Add(MoveTemp(Row));
			}
		}

		// Asset-owned texts (speaker name fields, shared words): same text pipeline as the
		// lines (state, stale diff, inline translation), counted into the Text bar. Click
		// opens the owning asset.
		auto AddAssetTextRow = [&](FAssetLines& RowGroup, const FKzLocText& Text, const FText& ClickTip)
		{
			FString ArchiveSource;
			FString ArchiveTranslation;
			const bool bHasEntry = Query.GetArchiveEntry(Text.Namespace, Text.Key, Culture, ArchiveSource, ArchiveTranslation);

			// The search matches the source text OR this culture's translation.
			if (!TextFilter.IsEmpty() && !Text.Source.Contains(TextFilter, ESearchCase::IgnoreCase) && !(bHasEntry && ArchiveTranslation.Contains(TextFilter, ESearchCase::IgnoreCase))) { return; }

			++TextTotal;

			FLineRow Row;
			Row.Label = FText::FromString(FlattenLabel(FString::Printf(TEXT("(%s) %s: %s"), *Text.Owner->GetName(), *Text.Field, *Text.Source)));
			Row.Translation = FText::FromString(bHasEntry ? ArchiveTranslation : FString());
			Row.TranslationCulture = Culture;
			Row.TranslationIdentities.Emplace(Text.Namespace, Text.Key);
			Row.TranslationSource = Text.Source;
			Row.SourceLocations.Add(Text.Owner->GetPathName());

			const EKzTranslationState State = Query.GetTextState(Text.Namespace, Text.Key, Text.Source, Culture);
			FillTextState(Row, State);
			if (State == EKzTranslationState::Translated)
			{
				++TextDone;
			}
			else
			{
				++PendingCount;
				PendingWords += CountWords(Text.Source);
			}
			if (State == EKzTranslationState::Stale)
			{
				++TextStale;
				if (bHasEntry) { Row.Tooltip = MakeStaleDiffTip(ArchiveSource, Text.Source, ArchiveTranslation); }
			}

			if (Row.Tooltip.IsEmpty())
			{
				Row.Tooltip = FText::Format(LOCTEXT("AssetTextRowTip", "{0}\n\n{1}"), Row.Label, ClickTip);
			}
			RowGroup.Lines.Add(MoveTemp(Row));
		};

		if (bShowDialogueLines)
		{
			FAssetLines SpeakerGroup;
			SpeakerGroup.bSpeakerGroup = true;
			FAssetLines WordGroup;
			WordGroup.bWordGroup = true;
			const FText SpeakerClickTip = LOCTEXT("SpeakerRowClickTip", "Click: open the speaker asset.");
			const FText WordClickTip = LOCTEXT("WordRowClickTip", "Click: open the word asset.");
			for (const FKzLocText& Text : OwnedTexts)
			{
				const bool bSpeaker = Text.Owner->IsA<UKzSpeakerAsset>();
				AddAssetTextRow(bSpeaker ? SpeakerGroup : WordGroup, Text, bSpeaker ? SpeakerClickTip : WordClickTip);
			}
			Groups.Add(MoveTemp(SpeakerGroup));
			Groups.Add(MoveTemp(WordGroup));
		}

		// Project texts, as their own group at the end of the culture's list. Identical
		// sources collapse into one row ("OK" x5): the bar still counts real archive
		// entries, but the words count once per unique text (the actual translation work)
		// and the Merge action can unify the identities into one.
		int32 OtherDone = 0;
		int32 OtherStale = 0;
		int32 OtherWords = 0;
		int32 OtherTotal = 0;
		if (!OtherTexts.IsEmpty())
		{
			FAssetLines& OtherGroup = Groups.AddDefaulted_GetRef();   // null asset = the Other texts area

			TMap<FString, TArray<int32>> IndicesBySource;
			TArray<FString> SourceOrder;
			for (int32 i = 0; i < OtherTexts.Num(); ++i)
			{
				TArray<int32>& Indices = IndicesBySource.FindOrAdd(OtherTexts[i].Source);
				if (Indices.IsEmpty()) { SourceOrder.Add(OtherTexts[i].Source); }
				Indices.Add(i);
			}

			for (const FString& Source : SourceOrder)
			{
				const TArray<int32>& Indices = IndicesBySource[Source];

				// The search matches the source text OR any group member's translation here.
				if (!TextFilter.IsEmpty() && !Source.Contains(TextFilter, ESearchCase::IgnoreCase))
				{
					bool bTranslationMatch = false;
					for (const int32 Index : Indices)
					{
						FString ArchiveSource;
						FString ArchiveTranslation;
						if (Query.GetArchiveEntry(OtherTexts[Index].Namespace, OtherTexts[Index].Key, Culture, ArchiveSource, ArchiveTranslation) && ArchiveTranslation.Contains(TextFilter, ESearchCase::IgnoreCase))
						{
							bTranslationMatch = true;
							break;
						}
					}
					if (!bTranslationMatch) { continue; }
				}

				int32 GroupMissing = 0;
				int32 GroupStale = 0;
				FString IdentityList;
				FText SingleStaleTooltip;
				for (const int32 Index : Indices)
				{
					const FOtherText& Text = OtherTexts[Index];
					++OtherTotal;
					IdentityList += FString::Printf(TEXT("%s,%s\n"), *Text.Namespace, *Text.Key);

					switch (Query.GetTextState(Text.Namespace, Text.Key, Text.Source, Culture))
					{
					case EKzTranslationState::Translated:
						++OtherDone;
						break;
					case EKzTranslationState::Stale:
					{
						++OtherStale;
						++GroupStale;
						FString ArchiveSource;
						FString ArchiveTranslation;
						if (Indices.Num() == 1 && Query.GetArchiveEntry(Text.Namespace, Text.Key, Culture, ArchiveSource, ArchiveTranslation))
						{
							SingleStaleTooltip = MakeStaleDiffTip(ArchiveSource, Text.Source, ArchiveTranslation);
						}
						break;
					}
					default:
						++GroupMissing;
						break;
					}
				}

				// The mergeable filter keeps only collapsed groups; singles still count in the bar.
				if (bOnlyMergeableTexts && Indices.Num() < 2)
				{
					continue;
				}

				const FString Preview = FlattenLabel(Source);

				FLineRow Row;
				// Count in front: the ellipsis cuts the tail of long labels, never the marker.
				Row.Label = Indices.Num() > 1
					? FText::Format(LOCTEXT("OtherTextCollapsed", "(x{1})  {0}"), FText::FromString(Preview), Indices.Num())
					: FText::FromString(Preview);

				Row.GroupSource = Source;
				Row.bCanMakeNonLocalizable = true;
				for (const int32 Index : Indices)
				{
					Row.GroupIdentities.Emplace(OtherTexts[Index].Namespace, OtherTexts[Index].Key);
					Row.SourceLocations.Append(Query.GetSourceLocations(OtherTexts[Index].Namespace, OtherTexts[Index].Key));
				}

				// Editable like the line rows: a commit writes every identity of the group, which
				// keeps collapsed identical sources sharing one translation. Shown value = the
				// first identity's current translation.
				Row.TranslationCulture = Culture;
				Row.TranslationIdentities = Row.GroupIdentities;
				Row.TranslationSource = Source;
				{
					FString ArchiveSource;
					FString GroupTranslation;
					Query.GetArchiveEntry(OtherTexts[Indices[0]].Namespace, OtherTexts[Indices[0]].Key, Culture, ArchiveSource, GroupTranslation);
					Row.Translation = FText::FromString(GroupTranslation);
				}

				// Owners visible without clicking: asset package paths (deduped) and code sites.
				FString AuthoredIn;
				{
					TArray<FString> Owners;
					for (const FString& Location : Row.SourceLocations)
					{
						FString Owner = Location;
						int32 DotIndex = INDEX_NONE;
						if (Owner.StartsWith(TEXT("/")) && Owner.FindChar(TEXT('.'), DotIndex)) { Owner.LeftInline(DotIndex); }
						Owners.AddUnique(Owner);
					}
					AuthoredIn = FString::Join(Owners, TEXT("\n"));
				}

				const FText BaseTip = !SingleStaleTooltip.IsEmpty()
					? SingleStaleTooltip
					: FText::Format(LOCTEXT("OtherTextIdentityTip", "{0}\n{1}"), FText::FromString(IdentityList.TrimEnd()), FText::FromString(Source));
				Row.Tooltip = FText::Format(LOCTEXT("OtherTextAuthoredTip", "{0}\n\nAuthored in:\n{1}\n\nClick: opens where this text is authored."), BaseTip, FText::FromString(AuthoredIn));

				// The group is as far along as its least done member.
				FillTextState(Row, GroupMissing > 0 ? EKzTranslationState::Missing : GroupStale > 0 ? EKzTranslationState::Stale : EKzTranslationState::Translated);
				if (Row.bPending)
				{
					++PendingCount;
					OtherWords += CountWords(Source);
				}

				OtherGroup.Lines.Add(MoveTemp(Row));
			}
		}

		if (bOnlyIncomplete && PendingCount == 0)
		{
			continue;
		}

		FCultureCard& Card = Cards.AddDefaulted_GetRef();
		Card.Title = FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture);
		Card.Culture = Culture;
		Card.KeyPrefix = Culture;
		if (bShowDialogueLines)
		{
			Card.Bars.Add({ LOCTEXT("BarText", "Text"), TextDone, TextTotal, TextStale, PendingWords });
			if (AudioTotal > 0)
			{
				Card.Bars.Add({ LOCTEXT("BarAudio", "Audio"), AudioDone, AudioTotal });
			}
		}
		if (OtherTotal > 0)
		{
			Card.Bars.Add({ LOCTEXT("BarOtherTexts", "Other"), OtherDone, OtherTotal, OtherStale, OtherWords });
		}
		Card.Groups = MoveTemp(Groups);
	}

	return Cards;
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::MakeCultureCard(FCultureCard Card)
{
	const FString AudioKeyPrefix = Card.KeyPrefix;
	const FString CardCulture = Card.Culture;

	// Shared so the lazily built area bodies can still reach the rows.
	const TSharedRef<TArray<FAssetLines>> Groups = MakeShared<TArray<FAssetLines>>(MoveTemp(Card.Groups));

	// Progress (badge + bars) always reflects every line; the ok-filters below only shape the list.
	int32 PendingCount = 0;
	for (const FAssetLines& Group : *Groups)
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
				.Text(Card.Title)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
				.Text(PendingCount == 0 ? LOCTEXT("CardComplete", "complete") : FText::Format(LOCTEXT("CardPendingCount", "{0} pending"), PendingCount))
				.ColorAndOpacity(FSlateColor(PendingCount == 0 ? KzDoneColor : KzPartialColor))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f))
				.ToolTipText(FText::Format(LOCTEXT("CompileCardTip", "Compile Text for '{0}' only: writes just this culture's .locres."), FText::FromString(CardCulture)))
				.OnClicked_Lambda([this, CardCulture]() { CompileCulture(CardCulture); return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Package")).ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		]
	];

	for (const FProgressBar& Bar : Card.Bars)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[ MakeProgressRow(Bar.Label, Bar.Done, Bar.Total, Bar.Stale, Bar.PendingWords) ];
	}

	// The ok-filters shape which rows show; the counters above ignore them on purpose.
	// Dialogue lines, speakers, words and the project-texts group render as SEPARATE areas.
	const FAssetLines* OtherGroup = nullptr;
	const FAssetLines* SpeakerGroup = nullptr;
	const FAssetLines* WordGroup = nullptr;
	TArray<const FAssetLines*> AssetGroups;
	for (const FAssetLines& Group : *Groups)
	{
		if (Group.Asset.IsValid()) { AssetGroups.Add(&Group); }
		else if (Group.bSpeakerGroup) { SpeakerGroup = &Group; }
		else if (Group.bWordGroup) { WordGroup = &Group; }
		else { OtherGroup = &Group; }
	}

	auto FilterVisible = [this](const FAssetLines& Group)
	{
		TArray<const FLineRow*> Visible;
		for (const FLineRow& Row : Group.Lines)
		{
			if (bOnlyIncomplete && !Row.bPending) { continue; }
			if (bOnlyMissingVoice && !Row.bAudioWork) { continue; }
			Visible.Add(&Row);
		}
		return Visible;
	};

	using FChildAreas = TArray<TPair<TWeakPtr<SExpandableArea>, FString>>;

	auto AddArea = [this, &Content, AudioKeyPrefix](const FText& Title, const FString& AreaName, TFunction<TSharedRef<SWidget>()> BuildBody, TSharedPtr<FChildAreas> ChildAreas)
	{
		const FString AreaKey = AudioKeyPrefix + TEXT("|") + AreaName;
		const bool bExpanded = IsAreaExpanded(AreaKey, /*bDefault=*/false);
		const TSharedRef<FKzLazyBody> Body = MakeLazyBody([BuildBody = MoveTemp(BuildBody)]() -> TSharedRef<SWidget>
		{
			// Recessed inset behind the row cells, so the list reads as its own region.
			return SNew(SBorder)
				.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.GroupBorder"))
				.Padding(4.0f)
				[
					BuildBody()
				];
		}, bExpanded);

		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(!bExpanded)
				.AreaTitle(Title)
				// Expansion survives the rebuild-happy Refresh; shift-click mirrors the engine
				// and cascades the new state to every sub-section inside.
				.OnAreaExpansionChanged_Lambda([this, AreaKey, ChildAreas, Body](bool bNowExpanded)
				{
					if (bNowExpanded) { Body->Ensure(); }
					AreaExpansion.Add(AreaKey, bNowExpanded);
					if (ChildAreas.IsValid() && FSlateApplication::Get().GetModifierKeys().IsShiftDown())
					{
						for (const TPair<TWeakPtr<SExpandableArea>, FString>& Child : *ChildAreas)
						{
							if (TSharedPtr<SExpandableArea> Area = Child.Key.Pin()) { Area->SetExpanded(bNowExpanded); }
							AreaExpansion.Add(Child.Value, bNowExpanded);
						}
					}
				})
				.BodyContent()
				[
					Body->Box
				]
		];
	};

	// A collapsible sub-section inside an area (one asset, one namespace), open by default.
	auto MakeSubArea = [this](const FString& AreaKey, TSharedRef<SWidget> Header, TFunction<TSharedRef<SWidget>()> BuildBody, FChildAreas& OutAreas) -> TSharedRef<SWidget>
	{
		const bool bExpanded = IsAreaExpanded(AreaKey, /*bDefault=*/true);
		const TSharedRef<FKzLazyBody> Body = MakeLazyBody(MoveTemp(BuildBody), bExpanded);
		TSharedPtr<SExpandableArea> Area;
		SAssignNew(Area, SExpandableArea)
			.InitiallyCollapsed(!bExpanded)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.OnAreaExpansionChanged_Lambda([this, AreaKey, Body](bool bNowExpanded)
			{
				if (bNowExpanded) { Body->Ensure(); }
				AreaExpansion.Add(AreaKey, bNowExpanded);
			})
			.HeaderContent()
			[
				Header
			]
			.BodyContent()
			[
				Body->Box
			];
		OutAreas.Emplace(Area, AreaKey);
		return Area.ToSharedRef();
	};

	// The row lists are built inside the lazy bodies; Groups keeps the rows they point at alive.
	auto MakeRowList = [this, Groups, AudioKeyPrefix](TArray<const FLineRow*> InRows, TWeakObjectPtr<UKzDialogueAsset> InAsset)
	{
		return [this, Groups, AudioKeyPrefix, InRows = MoveTemp(InRows), InAsset]() -> TSharedRef<SWidget>
		{
			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			for (const FLineRow* RowPtr : InRows)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					MakeLineRowWidget(*RowPtr, InAsset, AudioKeyPrefix)
				];
			}
			return Box;
		};
	};

	// Asset header rows only when several assets share the list; each asset is then its own
	// collapsible sub-section (the header still opens the asset on double-click).
	TArray<TPair<const FAssetLines*, TArray<const FLineRow*>>> VisibleAssetGroups;
	int32 VisibleCount = 0;
	for (const FAssetLines* Group : AssetGroups)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*Group);
		if (Visible.IsEmpty()) { continue; }
		VisibleCount += Visible.Num();
		VisibleAssetGroups.Emplace(Group, MoveTemp(Visible));
	}
	if (VisibleCount > 0)
	{
		const TSharedPtr<FChildAreas> AssetAreas = MakeShared<FChildAreas>();
		const bool bAssetHeaders = AssetGroups.Num() > 1;
		AddArea(FText::Format(LOCTEXT("LinesArea", "Lines ({0})"), VisibleCount), TEXT("Lines"),
			[this, VisibleAssetGroups = MoveTemp(VisibleAssetGroups), AssetAreas, bAssetHeaders, AudioKeyPrefix, MakeRowList, MakeSubArea]() -> TSharedRef<SWidget>
			{
				TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);
				for (int32 i = 0; i < VisibleAssetGroups.Num(); ++i)
				{
					const TWeakObjectPtr<UKzDialogueAsset> Asset = VisibleAssetGroups[i].Key->Asset;
					auto BuildRows = MakeRowList(VisibleAssetGroups[i].Value, Asset);
					if (!bAssetHeaders)
					{
						ListBox->AddSlot().AutoHeight()[ BuildRows() ];
						continue;
					}
					const FString AreaKey = AudioKeyPrefix + TEXT("|Asset|") + (Asset.IsValid() ? Asset->GetPathName() : FString());
					ListBox->AddSlot().AutoHeight().Padding(0.0f, i > 0 ? 6.0f : 1.0f, 0.0f, 1.0f)
					[
						MakeSubArea(AreaKey, MakeAssetHeaderRow(Asset), MoveTemp(BuildRows), *AssetAreas)
					];
				}
				return ListBox;
			}, AssetAreas);
	}

	if (SpeakerGroup)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*SpeakerGroup);
		if (!Visible.IsEmpty())
		{
			const int32 Count = Visible.Num();
			AddArea(FText::Format(LOCTEXT("SpeakersArea", "Speakers ({0})"), Count), TEXT("Speakers"), MakeRowList(MoveTemp(Visible), nullptr), nullptr);
		}
	}

	if (WordGroup)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*WordGroup);
		if (!Visible.IsEmpty())
		{
			const int32 Count = Visible.Num();
			AddArea(FText::Format(LOCTEXT("WordsArea", "Words ({0})"), Count), TEXT("Words"), MakeRowList(MoveTemp(Visible), nullptr), nullptr);
		}
	}

	if (OtherGroup)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*OtherGroup);
		if (!Visible.IsEmpty())
		{
			// Namespace buckets are collapsible sub-sections like the per-asset ones; the
			// area's shift-click cascades into them the same way.
			const int32 Count = Visible.Num();
			const TSharedPtr<FChildAreas> NamespaceAreas = MakeShared<FChildAreas>();
			AddArea(FText::Format(LOCTEXT("OtherTextsArea", "Other texts ({0})"), Count), TEXT("Other"),
				[Visible = MoveTemp(Visible), NamespaceAreas, AudioKeyPrefix, MakeRowList, MakeSubArea]() -> TSharedRef<SWidget>
				{
					// Grouped under namespace headers, like the lines group under asset headers. A
					// collapsed row spanning several namespaces (cross-namespace merge candidate)
					// sits under its first identity's namespace; the tooltip lists them all.
					TMap<FString, TArray<const FLineRow*>> RowsByNamespace;
					TArray<FString> NamespaceOrder;
					for (const FLineRow* RowPtr : Visible)
					{
						const FString Namespace = RowPtr->GroupIdentities.Num() > 0 ? RowPtr->GroupIdentities[0].Key : FString();
						TArray<const FLineRow*>& Bucket = RowsByNamespace.FindOrAdd(Namespace);
						if (Bucket.IsEmpty()) { NamespaceOrder.Add(Namespace); }
						Bucket.Add(RowPtr);
					}
					NamespaceOrder.Sort();

					TSharedRef<SVerticalBox> OtherBox = SNew(SVerticalBox);
					for (int32 i = 0; i < NamespaceOrder.Num(); ++i)
					{
						const FString& Namespace = NamespaceOrder[i];
						auto BuildRows = MakeRowList(RowsByNamespace[Namespace], nullptr);
						if (NamespaceOrder.Num() == 1)
						{
							OtherBox->AddSlot().AutoHeight()[ BuildRows() ];
							continue;
						}
						const TSharedRef<SWidget> Header = SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
							.Padding(FMargin(6.0f, 3.0f))
							[
								SNew(STextBlock)
									.Text(Namespace.IsEmpty() ? LOCTEXT("NoNamespaceHeader", "(no namespace)") : FText::FromString(Namespace))
									.Font(FAppStyle::GetFontStyle("BoldFont"))
							];
						OtherBox->AddSlot().AutoHeight().Padding(0.0f, i > 0 ? 6.0f : 1.0f, 0.0f, 1.0f)
						[
							MakeSubArea(AudioKeyPrefix + TEXT("|Ns|") + Namespace, Header, MoveTemp(BuildRows), *NamespaceAreas)
						];
					}
					return OtherBox;
				}, NamespaceAreas);
		}
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
			.OnClicked_Lambda([PlayKey, Path = Entry.AudioPath]()
			{
				if (KzDialogueAudition::IsPlaying(PlayKey)) { KzDialogueAudition::Stop(PlayKey); }
				else { KzDialogueAudition::Play(Cast<USoundBase>(Path.TryLoad()), PlayKey); }
				return FReply::Handled();
			})
			[
				SNew(SImage)
					.Image_Lambda([PlayKey]() { return FAppStyle::GetBrush(KzDialogueAudition::IsPlaying(PlayKey) ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"); })
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
	else if (Entry.bCanMakeNonLocalizable)
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
					.ToolTipText(LOCTEXT("MakeNonLocTip", "Make non-localizable: the text becomes culture invariant (no key, never gathered, no translation work). Rewrites the authored text in place; save the dirty assets and re-run Gather afterwards. Reversible by editing the text's localization settings."))
					.OnClicked_Lambda([this, InAsset, LineId = Entry.LineId, Source = Entry.GroupSource, Identities = Entry.GroupIdentities]()
					{
						MakeRowNonLocalizable(InAsset, LineId, Source, Identities);
						return FReply::Handled();
					})
					[
						SNew(SBox).WidthOverride(13.0f).HeightOverride(13.0f)
						[
							SNew(SImage).Image(FAppStyle::GetBrush("Icons.Unlink")).ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
			];
	}

	// Line-text merge actions join the state chip (the play column may hold a take): merge
	// collapses identical-source lines into one shared localization identity, unmerge gives
	// this line its own back. Text only; audio and timing are per line and unrelated.
	if (Entry.bSharedText || Entry.LineMergeGroup.Num() > 1)
	{
		TSharedRef<SWidget> Action = Entry.bSharedText
			? SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(1.0f))
				.ToolTipText(LOCTEXT("UnmergeLineTip", "Unmerge: give this line its own localization identity again, seeded with the shared translation. Save the dirty asset and re-run Gather afterwards."))
				.OnClicked_Lambda([this, InAsset, LineId = Entry.LineId]()
				{
					UnmergeLineRowText(InAsset, LineId);
					return FReply::Handled();
				})
				[
					SNew(SBox).WidthOverride(13.0f).HeightOverride(13.0f)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Unlink")).ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			: SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(1.0f))
				.ToolTipText(LOCTEXT("MergeLinesTip", "Merge texts: these identical lines share ONE localization identity and translation. The lines stay separate (audio and timing untouched); editing one of the texts detaches it again automatically. Save the dirty assets and re-run Gather afterwards."))
				.OnClicked_Lambda([this, Group = Entry.LineMergeGroup]()
				{
					MergeLineRowTexts(Group);
					return FReply::Handled();
				})
				[
					SNew(SBox).WidthOverride(13.0f).HeightOverride(13.0f)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Merge")).ColorAndOpacity(FSlateColor::UseForeground())
					]
				];

		ChipContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				ChipContent
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 6.0f, 0.0f)
			[
				Action
			];
	}

	// Collapsed identical-source project texts reuse the (empty for them) play column for
	// the Merge action, keeping every label aligned.
	if (Entry.GroupIdentities.Num() > 1)
	{
		PlayWidget = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText(LOCTEXT("MergeTextsTip", "Merge: rewrite every asset-authored occurrence so they all share one namespace/key - a single text for the localization system (the identity with the most translations wins). C++-authored texts are skipped. Save the dirty assets and re-run Gather afterwards."))
			.OnClicked_Lambda([this, Source = Entry.GroupSource, Identities = Entry.GroupIdentities]()
			{
				MergeOtherTexts(Source, Identities);
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Merge")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	// Fluid keyboard editing: the editor registers under a stable key so Enter/Tab can hop
	// to the next one even after the post-commit rebuild replaced every widget.
	const FString EditorKey = (!Entry.TranslationCulture.IsEmpty() && Entry.TranslationIdentities.Num() > 0)
		? Entry.TranslationCulture + TEXT("|") + Entry.TranslationIdentities[0].Key + TEXT("|") + Entry.TranslationIdentities[0].Value
		: FString();
	TSharedPtr<SEditableText> TranslationEditor;

	TSharedRef<SWidget> RowWidget = SNew(SBorder)
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
					.ToolTipText(Entry.Tooltip.IsEmpty() ? FText::Format(LOCTEXT("RowLabelTip", "{0}\n\nClick: select this line in the Lines tab."), Entry.Label) : Entry.Tooltip)
					.OnClicked_Lambda([this, InAsset, LineId = Entry.LineId, Locations = Entry.SourceLocations]()
					{
						// Project texts navigate to where they are authored; dialogue rows to their line.
						if (Locations.Num() > 0) { NavigateToTextSource(Locations); }
						else { NavigateToLine(InAsset, LineId); }
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
							.Text(Entry.Label)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			]
			// This culture's translation, edited in place: a commit writes the archive, Esc reverts, empty commits are ignored.
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)
			[
				SNew(SSeparator)
					.Orientation(Orient_Vertical)
					.Thickness(1.0f)
					.Visibility(Entry.TranslationCulture.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(2.0f, 0.0f, 4.0f, 0.0f)
			[
				SAssignNew(TranslationEditor, SEditableText)
					.Text(Entry.Translation)
					.HintText(LOCTEXT("TranslationHint", "untranslated"))
					.ToolTipText(Entry.Translation.IsEmpty()
						? LOCTEXT("TranslationEditTip", "Type the translation and press Enter or Tab: it is written into this culture's archive and the focus moves to the next row.")
						: Entry.Translation)
					.SelectAllTextWhenFocused(true)
					.RevertTextOnEscape(true)
					.ClearKeyboardFocusOnCommit(true)
					.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent)
					{
						// Tab routes through the commit (via the focus clear) so it saves AND advances like Enter.
						if (KeyEvent.GetKey() == EKeys::Tab)
						{
							bAdvanceTranslationFocus = true;
							FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					.OnTextCommitted_Lambda([this, Culture = Entry.TranslationCulture, Identities = Entry.TranslationIdentities, TranslationSource = Entry.TranslationSource, Previous = Entry.Translation, EditorKey](const FText& NewText, ETextCommit::Type CommitType)
					{
						const bool bAdvance = CommitType == ETextCommit::OnEnter || bAdvanceTranslationFocus;
						bAdvanceTranslationFocus = false;
						if (CommitType == ETextCommit::OnCleared) { return; }

						// The text box hands multi-line text back with CRLF; the archive stores LF.
						const FString NewTranslation = NewText.ToString().Replace(TEXT("\r\n"), TEXT("\n"));
						if (!NewTranslation.IsEmpty() && NewTranslation != Previous.ToString().Replace(TEXT("\r\n"), TEXT("\n")))
						{
							CommitTranslation(Culture, Identities, TranslationSource, NewTranslation, bAdvance ? NextEditorKeysAfter(EditorKey) : TArray<FString>());
						}
						else if (bAdvance)
						{
							// Deferred a tick: the editor clears the keyboard focus right after this callback.
							FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis = TWeakPtr<SKzDialogueCoveragePanel>(SharedThis(this)), Keys = NextEditorKeysAfter(EditorKey)](float)
							{
								if (TSharedPtr<SKzDialogueCoveragePanel> Panel = WeakThis.Pin())
								{
									Panel->FocusTranslationEditor(Keys);
								}
								return false;
							}));
						}
					})
					.Visibility(Entry.TranslationCulture.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
		];

	if (!EditorKey.IsEmpty() && TranslationEditor.IsValid())
	{
		TranslationEditors.Emplace(EditorKey, TranslationEditor);
	}
	return RowWidget;
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

void SKzDialogueCoveragePanel::NavigateToTextSource(const TArray<FString>& Locations)
{
	// Split asset locations (object paths) from code sites.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> OwnerAssets;
	FString CodeSite;
	for (const FString& Location : Locations)
	{
		if (Location.StartsWith(TEXT("/")))
		{
			FString PackageName = Location;
			int32 DotIndex = INDEX_NONE;
			if (PackageName.FindChar(TEXT('.'), DotIndex)) { PackageName.LeftInline(DotIndex); }

			TArray<FAssetData> PackageAssets;
			Registry.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				// The primary asset (name == package name), not whatever the registry lists first.
				const FAssetData* Primary = PackageAssets.FindByPredicate([](const FAssetData& Data) { return Data.IsUAsset(); });
				OwnerAssets.AddUnique(Primary ? *Primary : PackageAssets[0]);
			}
		}
		else if (CodeSite.IsEmpty())
		{
			CodeSite = Location;
		}
	}

	if (OwnerAssets.Num() == 1 && GEditor)
	{
		// Deferred one tick: loading from inside the click handler can land the widget
		// blueprint compile in a recursive load flush with half-loaded dependencies.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Path = OwnerAssets[0].GetSoftObjectPath()](float)
		{
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Path);
			}
			return false;   // one-shot
		}));
		return;
	}
	if (OwnerAssets.Num() > 1)
	{
		// Several owners (collapsed duplicates): show them all instead of opening N editors.
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowser.Get().SyncBrowserToAssets(OwnerAssets, /*bAllowLockedBrowsers=*/true);
		return;
	}

	// Code-authored text: best-effort jump to the source site ("<path> - line N" or "<path>(N)").
	if (!CodeSite.IsEmpty())
	{
		FString FilePath = CodeSite;
		int32 LineNumber = 1;
		FString LineText;
		if (CodeSite.Split(TEXT(" - line "), &FilePath, &LineText))
		{
			LineNumber = FCString::Atoi(*LineText);
		}
		else
		{
			int32 ParenIndex = INDEX_NONE;
			if (CodeSite.FindLastChar(TEXT('('), ParenIndex))
			{
				FilePath = CodeSite.Left(ParenIndex);
				LineNumber = FCString::Atoi(*CodeSite.Mid(ParenIndex + 1));
			}
		}
		FSourceCodeNavigation::OpenSourceFile(FPaths::ConvertRelativePathToFull(FilePath), LineNumber);
	}
}

void SKzDialogueCoveragePanel::MakeRowNonLocalizable(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId, FString Source, TArray<TPair<FString, FString>> Identities)
{
	// Dialogue line: turn its Text culture invariant on the owning asset. RebindFTextKeys
	// and the validators already respect the invariant, so it simply leaves the pipeline.
	if (LineId.IsValid())
	{
		UKzDialogueAsset* Asset = InAsset.Get();
		if (!Asset) { return; }
		const int32 LineIndex = Asset->IndexOfLine(LineId);
		if (LineIndex == INDEX_NONE) { return; }

		const FScopedTransaction Transaction(LOCTEXT("NonLocLineTrans", "Make Line Text Non-Localizable"));
		Asset->Modify();
		FText& Text = Asset->Lines[LineIndex].Text;
		const FString* SourceString = FTextInspector::GetSourceString(Text);
		Text = FText::AsCultureInvariant(SourceString ? *SourceString : Text.ToString());
		Asset->PostEditChange();
		Asset->MarkPackageDirty();

		RequestRefresh();
		return;
	}

	// Project text: rewrite every authored occurrence (same machinery as Merge).
	FText Error;
	int32 Rewritten = 0;
	int32 Skipped = 0;
	TArray<TPair<FString, FString>> Handled;
	const bool bRan = FKzDialogueTranslationCsv::MakeTextsNonLocalizable(Source, Identities, Error, Rewritten, Skipped, &Handled);

	// Fully resolved identities leave the view right away instead of waiting for the next
	// Gather; ones with skipped occurrences (C++ sites, unreachable storage) stay visible.
	for (const TPair<FString, FString>& Identity : Handled)
	{
		HandledIdentities.Add(Identity.Key + TEXT(",") + Identity.Value);
	}

	FNotificationInfo Info(bRan
		? FText::Format(LOCTEXT("NonLocDone", "Non-localizable: {0} occurrence(s) rewritten, {1} skipped (see the output log). Save the dirty assets and run Gather to drop them from the target."), Rewritten, Skipped)
		: Error);
	Info.ExpireDuration = 8.0f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bRan && (Rewritten > 0 || Handled.Num() > 0) ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	RequestRefresh();
}

void SKzDialogueCoveragePanel::MergeOtherTexts(FString Source, TArray<TPair<FString, FString>> Identities)
{
	FText Error;
	int32 Rewritten = 0;
	int32 Skipped = 0;
	TArray<TPair<FString, FString>> Handled;
	const bool bRan = FKzDialogueTranslationCsv::MergeIdenticalTexts(Source, Identities, Error, Rewritten, Skipped, &Handled);

	// Fully merged identities leave the view right away; the canonical one stays as the single
	// row. Ones with skipped occurrences (C++ sites, unreachable storage) stay visible.
	for (const TPair<FString, FString>& Identity : Handled)
	{
		HandledIdentities.Add(Identity.Key + TEXT(",") + Identity.Value);
	}

	FNotificationInfo Info(bRan
		? FText::Format(LOCTEXT("MergeDone", "Merge: {0} occurrence(s) re-keyed, {1} skipped (see the output log). Save the dirty assets and run Gather to see them as one text."), Rewritten, Skipped)
		: Error);
	Info.ExpireDuration = 8.0f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bRan && (Rewritten > 0 || Handled.Num() > 0) ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	RequestRefresh();
}

void SKzDialogueCoveragePanel::MergeLineRowTexts(const TArray<TPair<TWeakObjectPtr<UKzDialogueAsset>, FGuid>>& Group)
{
	TArray<FKzDialogueTranslationCsv::FKzLineTextRef> Lines;
	for (const TPair<TWeakObjectPtr<UKzDialogueAsset>, FGuid>& Entry : Group)
	{
		if (UKzDialogueAsset* Asset = Entry.Key.Get())
		{
			Lines.Add({ Asset, Entry.Value });
		}
	}

	FText Error;
	const bool bRan = FKzDialogueTranslationCsv::MergeLineTexts(Lines, Error);

	FNotificationInfo Info(bRan
		? FText::Format(LOCTEXT("MergeLinesDone", "Merged {0} line texts into one shared identity. Save the dirty assets and run Gather to see them as one text."), FText::AsNumber(Lines.Num()))
		: Error);
	Info.ExpireDuration = 8.0f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bRan ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	RequestRefresh();
}

void SKzDialogueCoveragePanel::UnmergeLineRowText(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId)
{
	FText Error;
	const bool bRan = FKzDialogueTranslationCsv::UnmergeLineText(InAsset.Get(), LineId, Error);

	FNotificationInfo Info(bRan
		? LOCTEXT("UnmergeLineDone", "The line has its own localization identity again. Save the dirty asset and re-run Gather.")
		: Error);
	Info.ExpireDuration = 8.0f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bRan ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	RequestRefresh();
}

void SKzDialogueCoveragePanel::CompileCulture(const FString& Culture)
{
	ULocalizationTarget* Target = ILocalizationModule::Get().GetLocalizationTargetByName(GetDefault<UKzDialogueSettings>()->LocalizationTargetName, /*bIsEngineTarget=*/false);
	if (!Target)
	{
		FNotificationInfo Info(LOCTEXT("NoLocTargetPanel", "Localization target not found. Create it once in the Localization Dashboard (Tools menu)."));
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return;
	}

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!Window.IsValid()) { Window = FSlateApplication::Get().GetActiveTopLevelWindow(); }
	if (!Window.IsValid()) { return; }

	// Compile only writes this culture's .locres; the archives the panel reads stay valid.
	LocalizationCommandletTasks::CompileTextForCulture(Window.ToSharedRef(), Target, Culture);
}

void SKzDialogueCoveragePanel::CommitTranslation(FString Culture, TArray<TPair<FString, FString>> Identities, FString Source, FString NewTranslation, TArray<FString> FocusAfter)
{
	FText Error;
	if (!FKzDialogueTranslationCsv::WriteTranslation(Culture, Identities, Source, NewTranslation, Error))
	{
		FNotificationInfo Info(Error);
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return;
	}

	// The archive save already requested the refresh; the focus lands on the rebuilt editors.
	RequestRefresh(MoveTemp(FocusAfter));
}

TArray<FString> SKzDialogueCoveragePanel::NextEditorKeysAfter(const FString& EditorKey) const
{
	TArray<FString> Keys;
	bool bFound = false;
	for (const TPair<FString, TWeakPtr<SEditableText>>& Editor : TranslationEditors)
	{
		if (bFound) { Keys.Add(Editor.Key); }
		else if (Editor.Key == EditorKey) { bFound = true; }
	}
	return Keys;
}

void SKzDialogueCoveragePanel::FocusTranslationEditor(const TArray<FString>& CandidateKeys)
{
	for (const FString& Key : CandidateKeys)
	{
		for (const TPair<FString, TWeakPtr<SEditableText>>& Editor : TranslationEditors)
		{
			if (Editor.Key == Key)
			{
				if (TSharedPtr<SEditableText> Widget = Editor.Value.Pin())
				{
					FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::SetDirectly);
					return;
				}
			}
		}
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

	RequestRefresh();
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

	// Culture-less: SourceText edits made in the sheet go back into the AUTHORED assets.
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ImportSourceFixes", "Source fixes (CSV)..."),
		LOCTEXT("ImportSourceFixesTip", "Apply SourceText edits made in an exported CSV back to the authored assets (dialogue lines, speaker fields, project texts). Rows whose asset changed after the export are skipped as conflicted; save the dirty assets and re-run Gather afterwards."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { FKzDialogueTranslationCsv::ImportSourceFixesInteractive(); RequestRefresh(); })));

	MenuBuilder.AddMenuSeparator();

	// Same culture list under each format; entries run the matching interactive import.
	auto AddCultureEntries = [this](FMenuBuilder& Sub, const TArray<FString>& Cultures, TFunction<void(const FString&)> Run)
	{
		for (const FString& Culture : Cultures)
		{
			Sub.AddMenuEntry(
				FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
				FText::Format(LOCTEXT("ImportCultureEntryTip", "Import a translated file into the '{0}' archive."), FText::FromString(Culture)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Culture, Run]() { Run(Culture); RequestRefresh(); })));
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
			FExecuteAction::CreateLambda([this]() { bOnlyMissingVoice = !bOnlyMissingVoice; SavePersistedFilter(TEXT("bOnlyMissingVoice"), bOnlyMissingVoice); RequestRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bOnlyMissingVoice; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OnlyIncomplete", "Only incomplete"),
		LOCTEXT("OnlyIncompleteTip", "Hide the 'ok' lines; cultures with nothing pending lose their whole card."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { bOnlyIncomplete = !bOnlyIncomplete; SavePersistedFilter(TEXT("bOnlyIncomplete"), bOnlyIncomplete); RequestRefresh(); }),
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
			FExecuteAction::CreateLambda([this]() { bShowLocalizedAudio = !bShowLocalizedAudio; SavePersistedFilter(TEXT("bShowLocalizedAudio"), bShowLocalizedAudio); RequestRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bShowLocalizedAudio; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	if (bIncludeProjectTexts)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowDialogueLines", "Dialogue lines"),
			LOCTEXT("ShowDialogueLinesTip", "Show the dialogue line areas and their bars. Turn off to focus on the project texts."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bShowDialogueLines = !bShowDialogueLines; SavePersistedFilter(TEXT("bShowDialogueLines"), bShowDialogueLines); RequestRefresh(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bShowDialogueLines; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOtherTexts", "Other texts"),
			LOCTEXT("ShowOtherTextsTip", "Show the target's non-dialogue gathered texts (UI, menus...) as their own area and progress bar on each culture card."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bShowOtherTexts = !bShowOtherTexts; SavePersistedFilter(TEXT("bShowOtherTexts"), bShowOtherTexts); RequestRefresh(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bShowOtherTexts; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OnlyMergeableTexts", "Only mergeable texts"),
			LOCTEXT("OnlyMergeableTextsTip", "Other texts show only identical-source groups (x2 and up): the Merge candidates."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bOnlyMergeableTexts = !bOnlyMergeableTexts; SavePersistedFilter(TEXT("bOnlyMergeableTexts"), bOnlyMergeableTexts); RequestRefresh(); }),
				FCanExecuteAction::CreateLambda([this]() { return bShowOtherTexts; }),
				FIsActionChecked::CreateLambda([this]() { return bOnlyMergeableTexts; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

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

	auto PassesText = [Filter = TextFilter](const FKzDialogueLine& Line)
	{
		if (Filter.IsEmpty()) { return true; }
		const FString* Source = FTextInspector::GetSourceString(Line.Text);
		return Source && Source->Contains(Filter, ESearchCase::IgnoreCase);
	};

	// Pending text is evaluated against the filtered culture, or every foreign culture when
	// the filter is off. The native culture has no text to translate.
	TSharedRef<FKzLocQuery> Query = MakeShared<FKzLocQuery>();
	FText QueryError;
	TArray<FString> PendingCultures;
	TArray<FString> ExportCultures;
	if (Query->Load(QueryError))
	{
		ExportCultures = Query->GetTarget().ForeignCultures;
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
			if (!PassesSpeaker(Line) || !PassesText(Line)) { continue; }
			++FilteredCount;
			if (!PendingCultures.IsEmpty() && IsTextPending(Line)) { ++PendingCount; }
		}
	}

	// Project texts that can ride along with the scope exports: every gathered non-dialogue
	// identity except the ones already handled from this panel (stale manifest leftovers).
	TSharedPtr<TSet<FString>> OtherIdentities;
	if (bIncludeProjectTexts)
	{
		OtherIdentities = MakeShared<TSet<FString>>();
		Query->EnumerateOtherTexts([this, &OtherIdentities](const FString& Namespace, const FString& Key, const FString&)
		{
			const FString Identity = Namespace + TEXT(",") + Key;
			if (!HandledIdentities.Contains(Identity)) { OtherIdentities->Add(Identity); }
		});
	}
	const int32 OtherCount = OtherIdentities.IsValid() ? OtherIdentities->Num() : 0;
	const TSharedPtr<TSet<FString>> RideAlong = bExportProjectTexts && OtherCount > 0 ? OtherIdentities : TSharedPtr<TSet<FString>>();

	// Both formats share the same scopes; each scope opens the culture list, like the Import
	// menu. The chosen culture pre-fills the file with its current translations and names it
	// (CSV "_es" suffix, PO per-culture folder); PO can also write every culture at once.
	auto AddScopeEntries = [AssetData, PassesSpeaker, PassesText, IsTextPending, AllCount, FilteredCount, PendingCount, ExportCultures, bFiltersActive = bSpeakerFilterActive || !TextFilter.IsEmpty()](FMenuBuilder& Sub, bool bAllCulturesEntry, TFunction<void(TArray<FAssetData>, const FKzExportLineFilter&, const FString&)> Run)
	{
		auto AddCultureLeaves = [AssetData, ExportCultures, Run](FMenuBuilder& CultureSub, bool bWithAllCultures, FKzExportLineFilter Filter, bool bEnabled)
		{
			if (bWithAllCultures)
			{
				CultureSub.AddMenuEntry(
					LOCTEXT("ExportEveryCulture", "All cultures"),
					LOCTEXT("ExportEveryCultureTip", "One file per foreign culture, each pre-filled with its translations."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([AssetData, Run, Filter]() { Run(AssetData, Filter, FString()); }),
						FCanExecuteAction::CreateLambda([bEnabled]() { return bEnabled; })));
			}
			else if (ExportCultures.IsEmpty())
			{
				CultureSub.AddMenuEntry(
					LOCTEXT("NoExportCultures", "No localization target cultures found"),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
			}
			for (const FString& Culture : ExportCultures)
			{
				CultureSub.AddMenuEntry(
					FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
					FText::Format(LOCTEXT("ExportCultureEntryTip", "Pre-fill the file with the current '{0}' translations."), FText::FromString(Culture)),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([AssetData, Run, Filter, Culture]() { Run(AssetData, Filter, Culture); }),
						FCanExecuteAction::CreateLambda([bEnabled]() { return bEnabled; })));
			}
		};

		Sub.AddSubMenu(
			FText::Format(LOCTEXT("ExportAllLines", "All lines ({0})"), AllCount),
			LOCTEXT("ExportAllLinesTip", "Export every line of the assets."),
			FNewMenuDelegate::CreateLambda([AddCultureLeaves, bAllCulturesEntry](FMenuBuilder& CultureSub)
			{
				AddCultureLeaves(CultureSub, bAllCulturesEntry, nullptr, true);
			}));

		Sub.AddSubMenu(
			FText::Format(LOCTEXT("ExportFilteredLines", "Filtered lines ({0})"), FilteredCount),
			LOCTEXT("ExportFilteredLinesTip", "Export only the lines passing the speaker and search filters."),
			FNewMenuDelegate::CreateLambda([AddCultureLeaves, bAllCulturesEntry, PassesSpeaker, PassesText, bFiltersActive](FMenuBuilder& CultureSub)
			{
				AddCultureLeaves(CultureSub, bAllCulturesEntry,
					[PassesSpeaker, PassesText](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line) && PassesText(Line); },
					bFiltersActive);
			}));

		Sub.AddSubMenu(
			FText::Format(LOCTEXT("ExportPendingLines", "Pending text ({0})"), PendingCount),
			LOCTEXT("ExportPendingLinesTip", "Export only the lines whose text is missing or stale in the cultures in scope (speaker and search filters apply)."),
			FNewMenuDelegate::CreateLambda([AddCultureLeaves, bAllCulturesEntry, PassesSpeaker, PassesText, IsTextPending, PendingCount](FMenuBuilder& CultureSub)
			{
				AddCultureLeaves(CultureSub, bAllCulturesEntry,
					[PassesSpeaker, PassesText, IsTextPending](const UKzDialogueAsset&, const FKzDialogueLine& Line) { return PassesSpeaker(Line) && PassesText(Line) && IsTextPending(Line); },
					PendingCount > 0);
			}));
	};

	MenuBuilder.AddSubMenu(
		LOCTEXT("ExportCsvSub", "Translation CSV"),
		LOCTEXT("ExportCsvSubTip", "One CSV per chosen culture with the source texts, its current translations (stale ones as reference in the StaleTranslation column) and context columns (speaker, notes, max characters, audio, drift hash). Project texts join as asset-less rows while the check below is on."),
		FNewMenuDelegate::CreateLambda([AddScopeEntries, RideAlong](FMenuBuilder& Sub)
		{
			AddScopeEntries(Sub, /*bAllCulturesEntry=*/false, [RideAlong](TArray<FAssetData> Data, const FKzExportLineFilter& Filter, const FString& Culture)
			{
				FKzDialogueTranslationCsv::ExportInteractive(MoveTemp(Data), Filter, RideAlong, Culture);
			});
		}));

	MenuBuilder.AddSubMenu(
		LOCTEXT("ExportPoSub", "Portable Object (.po)"),
		LOCTEXT("ExportPoSubTip", "One .po per culture (the standard translation-tool format): context travels as comments, existing translations fill msgstr, stale ones are flagged fuzzy. Project texts join as <Target>_Other.po files while the check below is on."),
		FNewMenuDelegate::CreateLambda([AddScopeEntries, RideAlong](FMenuBuilder& Sub)
		{
			AddScopeEntries(Sub, /*bAllCulturesEntry=*/true, [RideAlong](TArray<FAssetData> Data, const FKzExportLineFilter& Filter, const FString& Culture)
			{
				FKzDialogueTranslationCsv::ExportPoInteractive(MoveTemp(Data), Filter, RideAlong.IsValid(), RideAlong, Culture);
			});
		}));

	// Everything the target gathers OUTSIDE the dialogue pipeline; project-scope hosts only.
	if (bIncludeProjectTexts)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("IncludeOtherTexts", "Include project texts ({0})"), OtherCount),
			LOCTEXT("IncludeOtherTextsTip", "The scope exports above also carry every gathered text outside the dialogue assets (UI, menus...): extra asset-less rows in the CSV, <Target>_Other.po files next to the PO ones."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bExportProjectTexts = !bExportProjectTexts; SavePersistedFilter(TEXT("bExportProjectTexts"), bExportProjectTexts); }),
				FCanExecuteAction::CreateLambda([OtherCount]() { return OtherCount > 0; }),
				FIsActionChecked::CreateLambda([this]() { return bExportProjectTexts; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("ExportOtherTexts", "Project texts only (.po) ({0})"), OtherCount),
			LOCTEXT("ExportOtherTextsTip", "Just the gathered texts outside the dialogue assets, one <Target>_Other.po per culture. The speaker and pending filters do not apply."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([OtherIdentities]() { FKzDialogueTranslationCsv::ExportPoInteractive(TArray<FAssetData>(), nullptr, true, OtherIdentities); }),
				FCanExecuteAction::CreateLambda([OtherCount]() { return OtherCount > 0; })));
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueCoveragePanel::BuildCultureFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AllCultures", "All Cultures"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { CultureFilter.Reset(); RequestRefresh(); })));

	FKzLocTargetInfo Target;
	FText Error;
	if (FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error))
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("NativeFilterEntry", "{0} - native"), FKzDialogueTranslationCsv::GetCultureDisplayLabel(Target.NativeCulture)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Culture = Target.NativeCulture]() { CultureFilter = Culture; RequestRefresh(); })));

		for (const FString& Culture : Target.ForeignCultures)
		{
			MenuBuilder.AddMenuEntry(
				FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Culture]() { CultureFilter = Culture; RequestRefresh(); })));
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
		FUIAction(FExecuteAction::CreateLambda([this]() { bSpeakerFilterActive = false; SpeakerFilter.Reset(); RequestRefresh(); })));

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
			FUIAction(FExecuteAction::CreateLambda([this]() { bSpeakerFilterActive = true; SpeakerFilter.Reset(); RequestRefresh(); })));
	}

	for (const FString& Name : Names)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Name),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Name]() { bSpeakerFilterActive = true; SpeakerFilter = Name; RequestRefresh(); })));
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE