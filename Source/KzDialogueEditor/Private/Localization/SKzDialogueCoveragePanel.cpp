// Copyright 2026 kirzo

#include "Localization/SKzDialogueCoveragePanel.h"

#include "KzDialogueAsset.h"
#include "Localization/KzDialogueTranslationCsv.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SKzDialogueCoveragePanel"

void SKzDialogueCoveragePanel::Construct(const FArguments& /*InArgs*/, UKzDialogueAsset* InAsset)
{
	Asset = InAsset;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("Title", "Translation coverage per culture"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(6.0f, 2.0f, 6.0f, 6.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(Rows, SVerticalBox)
			]
		]
	];

	Refresh();
}

void SKzDialogueCoveragePanel::Refresh()
{
	Rows->ClearChildren();

	auto AddMessage = [this](const FText& Message)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock).Text(Message).AutoWrapText(true)
		];
	};

	UKzDialogueAsset* AssetPtr = Asset.Get();
	if (!AssetPtr)
	{
		AddMessage(LOCTEXT("NoAsset", "No asset."));
		return;
	}

	TArray<FKzCultureCoverage> Cultures;
	TArray<FKzStaleTranslation> Stale;
	FText Error;
	if (!FKzDialogueTranslationCsv::BuildCoverage({ AssetPtr }, Cultures, Stale, Error))
	{
		AddMessage(Error);
		return;
	}
	if (Cultures.IsEmpty())
	{
		AddMessage(LOCTEXT("NoCultures", "The localization target has no foreign cultures."));
		return;
	}

	FillCoverageRows(*Rows, Cultures);
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