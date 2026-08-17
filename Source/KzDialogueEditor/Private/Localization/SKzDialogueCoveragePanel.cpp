// Copyright 2026 kirzo

#include "Localization/SKzDialogueCoveragePanel.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzLibEditorStyle.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
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
#include "Widgets/Input/SSearchBox.h"
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
	bIncludeProjectTexts = InArgs._bIncludeProjectTexts;

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
									.ToolTipText(LOCTEXT("SearchTip", "Show only the dialogue lines and project texts containing this text."))
									.DelayChangeNotificationsWhileTyping(true)
									.OnTextChanged_Lambda([this](const FText& NewText) { TextFilter = NewText.ToString(); Refresh(); })
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
			if (!bShowDialogueLines) { break; }

			FAssetLines& Group = Groups.AddDefaulted_GetRef();
			Group.Asset = AssetPtr;

			for (const FKzDialogueLine& Line : AssetPtr->Lines)
			{
				if (!PassesSpeaker(Line) || !PassesText(Line)) { continue; }

				bool bTextMissing = false;
				bool bTextStale = false;
				bool bTextTooLong = false;
				bool bAudioMissing = false;
				FText PendingTooltip;
				FString RowTranslation;
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
					if (bHasEntry) { RowTranslation = ArchiveTranslation; }

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
				Row.Translation = FText::FromString(RowTranslation);
				Row.AudioPath = LocalizedAudioPath;
				Row.bCanMakeNonLocalizable = !Line.Text.IsEmpty() && Namespace.IsSet() && Key.IsSet();
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
				if (!TextFilter.IsEmpty() && !Source.Contains(TextFilter, ESearchCase::IgnoreCase)) { continue; }

				const TArray<int32>& Indices = IndicesBySource[Source];

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
							SingleStaleTooltip = FText::Format(
								LOCTEXT("StaleDiffTip", "Translated against:\n{0}\n\nCurrent text:\n{1}\n\nCurrent translation:\n{2}"),
								FText::FromString(ArchiveSource), FText::FromString(Text.Source), FText::FromString(ArchiveTranslation));
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

				const FString Preview = Source.Len() > 60 ? Source.Left(60) + TEXT("...") : Source;

				FLineRow Row;
				Row.Label = Indices.Num() > 1
					? FText::Format(LOCTEXT("OtherTextCollapsed", "{0}   (x{1})"), FText::FromString(Preview), Indices.Num())
					: FText::FromString(Preview);

				Row.GroupSource = Source;
				Row.bCanMakeNonLocalizable = true;
				for (const int32 Index : Indices)
				{
					Row.GroupIdentities.Emplace(OtherTexts[Index].Namespace, OtherTexts[Index].Key);
					Row.SourceLocations.Append(Query.GetSourceLocations(OtherTexts[Index].Namespace, OtherTexts[Index].Key));
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

				if (GroupMissing > 0 || GroupStale > 0)
				{
					++PendingCount;
					Row.bPending = true;
					OtherWords += CountWords(Source);
					Row.State = GroupMissing > 0 ? LOCTEXT("PendingMissing", "text") : LOCTEXT("PendingStale", "stale");
					Row.StateColor = GroupMissing > 0 ? KzMissingColor : KzStaleColor;
				}
				else
				{
					Row.State = LOCTEXT("RowOk", "ok");
					Row.StateColor = KzDoneColor;
				}

				OtherGroup.Lines.Add(MoveTemp(Row));
			}
		}

		if (bOnlyIncomplete && PendingCount == 0)
		{
			continue;
		}

		TArray<TSharedRef<SWidget>> Bars;
		if (bShowDialogueLines)
		{
			Bars.Add(MakeProgressRow(LOCTEXT("BarText", "Text"), TextDone, TextTotal, TextStale, PendingWords));
			if (AudioTotal > 0)
			{
				Bars.Add(MakeProgressRow(LOCTEXT("BarAudio", "Audio"), AudioDone, AudioTotal, 0));
			}
		}
		if (OtherTotal > 0)
		{
			Bars.Add(MakeProgressRow(LOCTEXT("BarOtherTexts", "Other"), OtherDone, OtherTotal, OtherStale, OtherWords));
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
	// Dialogue lines and the project-texts group render as SEPARATE areas: they are not lines.
	const FAssetLines* OtherGroup = nullptr;
	TArray<const FAssetLines*> AssetGroups;
	for (const FAssetLines& Group : Groups)
	{
		if (Group.Asset.IsValid()) { AssetGroups.Add(&Group); }
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

	auto AddArea = [this, &Content](const FText& Title, TSharedRef<SVerticalBox> Box)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(Title)
				.BodyContent()
				[
					// Recessed inset behind the row cells, so the list reads as its own region.
					SNew(SBorder)
						.BorderImage(FKzLibEditorStyle::Get().GetBrush("Kz.GroupBorder"))
						.Padding(4.0f)
						[
							Box
						]
				]
		];
	};

	// Asset header rows only when several assets share the list; each asset is then its own
	// collapsible sub-section, open by default (the header still opens the asset on double-click).
	const bool bAssetHeaders = AssetGroups.Num() > 1;
	int32 VisibleCount = 0;
	TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);
	for (const FAssetLines* Group : AssetGroups)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*Group);
		if (Visible.IsEmpty()) { continue; }

		TSharedRef<SVerticalBox> GroupBox = SNew(SVerticalBox);
		for (const FLineRow* RowPtr : Visible)
		{
			GroupBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				MakeLineRowWidget(*RowPtr, Group->Asset, AudioKeyPrefix)
			];
		}

		if (bAssetHeaders)
		{
			ListBox->AddSlot().AutoHeight().Padding(0.0f, VisibleCount > 0 ? 6.0f : 1.0f, 0.0f, 1.0f)
			[
				SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.HeaderContent()
					[
						MakeAssetHeaderRow(Group->Asset)
					]
					.BodyContent()
					[
						GroupBox
					]
			];
		}
		else
		{
			ListBox->AddSlot().AutoHeight()
			[
				GroupBox
			];
		}
		VisibleCount += Visible.Num();
	}
	if (VisibleCount > 0)
	{
		AddArea(FText::Format(LOCTEXT("LinesArea", "Lines ({0})"), VisibleCount), ListBox);
	}

	if (OtherGroup)
	{
		TArray<const FLineRow*> Visible = FilterVisible(*OtherGroup);
		if (!Visible.IsEmpty())
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
			const bool bNamespaceHeaders = NamespaceOrder.Num() > 1;

			TSharedRef<SVerticalBox> OtherBox = SNew(SVerticalBox);
			int32 RowsAdded = 0;
			for (const FString& Namespace : NamespaceOrder)
			{
				if (bNamespaceHeaders)
				{
					OtherBox->AddSlot().AutoHeight().Padding(0.0f, RowsAdded > 0 ? 6.0f : 1.0f, 0.0f, 1.0f)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
							.Padding(FMargin(6.0f, 3.0f))
							[
								SNew(STextBlock)
									.Text(Namespace.IsEmpty() ? LOCTEXT("NoNamespaceHeader", "(no namespace)") : FText::FromString(Namespace))
									.Font(FAppStyle::GetFontStyle("BoldFont"))
							]
					];
				}
				for (const FLineRow* RowPtr : RowsByNamespace[Namespace])
				{
					OtherBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
					[
						MakeLineRowWidget(*RowPtr, nullptr, AudioKeyPrefix)
					];
					++RowsAdded;
				}
			}
			AddArea(FText::Format(LOCTEXT("OtherTextsArea", "Other texts ({0})"), Visible.Num()), OtherBox);
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
			// The culture's current translation, dimmed on the right; rows without one give the label the full width.
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(Entry.Translation)
					.Justification(ETextJustify::Right)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.ToolTipText(Entry.Translation)
					.Visibility(Entry.Translation.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
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

		Refresh();
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

	Refresh();
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

	Refresh();
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

	// Culture-less: SourceText edits made in the sheet go back into the AUTHORED assets.
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ImportSourceFixes", "Source fixes (CSV)..."),
		LOCTEXT("ImportSourceFixesTip", "Apply SourceText edits made in an exported CSV back to the authored assets (dialogue lines, speaker fields, project texts). Rows whose asset changed after the export are skipped as conflicted; save the dirty assets and re-run Gather afterwards."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { FKzDialogueTranslationCsv::ImportSourceFixesInteractive(); Refresh(); })));

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

	if (bIncludeProjectTexts)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowDialogueLines", "Dialogue lines"),
			LOCTEXT("ShowDialogueLinesTip", "Show the dialogue line areas and their bars. Turn off to focus on the project texts."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bShowDialogueLines = !bShowDialogueLines; Refresh(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bShowDialogueLines; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOtherTexts", "Other texts"),
			LOCTEXT("ShowOtherTextsTip", "Show the target's non-dialogue gathered texts (UI, menus...) as their own area and progress bar on each culture card."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bShowOtherTexts = !bShowOtherTexts; Refresh(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bShowOtherTexts; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OnlyMergeableTexts", "Only mergeable texts"),
			LOCTEXT("OnlyMergeableTextsTip", "Other texts show only identical-source groups (x2 and up): the Merge candidates."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { bOnlyMergeableTexts = !bOnlyMergeableTexts; Refresh(); }),
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
				FExecuteAction::CreateLambda([this]() { bExportProjectTexts = !bExportProjectTexts; }),
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