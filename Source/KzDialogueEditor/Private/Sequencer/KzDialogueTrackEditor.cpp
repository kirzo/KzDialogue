// Copyright 2026 kirzo

#include "Sequencer/KzDialogueTrackEditor.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "KzDialogueAsset.h"
#include "Sequencer/MovieSceneKzDialogueTrack.h"
#include "Sequencer/MovieSceneKzDialogueSection.h"

#include "ISequencer.h"
#include "ISequencerSection.h"
#include "SequencerUtilities.h"
#include "SequencerSectionPainter.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueTrackEditor"

// =======================================================================================
// Track editor
// =======================================================================================

FKzDialogueTrackEditor::FKzDialogueTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

TSharedRef<ISequencerTrackEditor> FKzDialogueTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FKzDialogueTrackEditor>(InSequencer);
}

bool FKzDialogueTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneKzDialogueTrack::StaticClass();
}

void FKzDialogueTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddTrack", "KzDialogue Track"),
		LOCTEXT("AddTrackTip", "Add a track that drives KzDialogue lines from a UKzDialogueAsset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
		FUIAction(FExecuteAction::CreateSP(this, &FKzDialogueTrackEditor::HandleAddTrackMenuEntry)));
}

void FKzDialogueTrackEditor::HandleAddTrackMenuEntry()
{
	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene) { return; }

	const FScopedTransaction Transaction(LOCTEXT("AddTrackTrans", "Add KzDialogue Track"));
	MovieScene->Modify();

	UMovieSceneKzDialogueTrack* NewTrack = MovieScene->AddTrack<UMovieSceneKzDialogueTrack>();
	if (NewTrack)
	{
		if (TSharedPtr<ISequencer> SequencerPin = GetSequencer())
		{
			SequencerPin->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		}
	}
}

TSharedPtr<SWidget> FKzDialogueTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	UMovieSceneKzDialogueTrack* DialogueTrack = Cast<UMovieSceneKzDialogueTrack>(Track);
	if (!DialogueTrack) { return SNullWidget::NullWidget; }

	// Use the same helper Sequencer's native track editors (Audio, Fade, ...) use.
	return FSequencerUtilities::MakeAddButton(
		LOCTEXT("AddLineSection", "Line"),
		FOnGetContent::CreateSP(this, &FKzDialogueTrackEditor::BuildAddSectionMenu, DialogueTrack),
		Params.NodeIsHovered,
		GetSequencer());
}

void FKzDialogueTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track)
{
	UMovieSceneKzDialogueTrack* DialogueTrack = Cast<UMovieSceneKzDialogueTrack>(Track);
	if (!DialogueTrack) { return; }

	MenuBuilder.AddSubMenu(
		LOCTEXT("AddSectionSub", "Add Line Section"),
		LOCTEXT("AddSectionSubTip", "Add a section for a specific line from the asset"),
		FNewMenuDelegate::CreateLambda([this, DialogueTrack](FMenuBuilder& SubBuilder)
			{
				// Reuse the same searchable picker.
				SubBuilder.AddWidget(BuildAddSectionMenu(DialogueTrack), FText::GetEmpty(), false);
			}));
}

TSharedRef<SWidget> FKzDialogueTrackEditor::BuildAddSectionMenu(UMovieSceneKzDialogueTrack* Track)
{
	UKzDialogueAsset* Asset = Track ? Track->DialogueAsset : nullptr;
	if (!IsValid(Asset))
	{
		// No asset yet — bail with a hint instead of an empty searchable list.
		return SNew(SBox)
			.Padding(FMargin(8.f))
			[
				SNew(STextBlock).Text(LOCTEXT("NoAsset", "Set DialogueAsset on the track first."))
			];
	}

	// Collect line ids already represented as sections on this track so the picker can
	// hide them (or just gray them out, depending on filter state).
	TSet<FGuid> AlreadyUsed;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (UMovieSceneKzDialogueSection* DlgSection = Cast<UMovieSceneKzDialogueSection>(Section))
		{
			AlreadyUsed.Add(DlgSection->LineId);
		}
	}

	return SNew(SKzDialogueLinePicker)
		.Asset(Asset)
		.AlreadyUsedLineIds(AlreadyUsed)
		.OnLinePicked(SKzDialogueLinePicker::FOnLinePicked::CreateSP(
			this, &FKzDialogueTrackEditor::HandleAddSection, Track));
}

void FKzDialogueTrackEditor::HandleAddSection(FGuid LineId, float DefaultDurationSeconds, UMovieSceneKzDialogueTrack* Track)
{
	if (!IsValid(Track) || !Track->GetTypedOuter<UMovieScene>()) { return; }

	UMovieScene* MovieScene = Track->GetTypedOuter<UMovieScene>();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();

	TSharedPtr<ISequencer> SequencerPin = GetSequencer();
	const FFrameNumber StartFrame = SequencerPin.IsValid()
		? SequencerPin->GetLocalTime().Time.FrameNumber
		: MovieScene->GetPlaybackRange().GetLowerBoundValue();

	const FFrameNumber EndFrame = StartFrame + (DefaultDurationSeconds * TickResolution).RoundToFrame();

	const FScopedTransaction Transaction(LOCTEXT("AddSectionTrans", "Add Dialogue Section"));
	Track->Modify();

	UMovieSceneKzDialogueSection* Section = NewObject<UMovieSceneKzDialogueSection>(Track, NAME_None, RF_Transactional);
	Section->LineId = LineId;
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));

	if (UKzDialogueAsset* Asset = Track->DialogueAsset)
	{
		FKzDialogueLine Line;
		if (Asset->TryGetLineById(LineId, Line))
		{
			Section->DisplayName = Line.Text.ToString().Left(60);
		}
	}

	Track->AddSection(*Section);

	if (SequencerPin.IsValid())
	{
		SequencerPin->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

TSharedRef<ISequencerSection> FKzDialogueTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid /*ObjectBinding*/)
{
	UMovieSceneKzDialogueSection* DialogueSection = CastChecked<UMovieSceneKzDialogueSection>(&SectionObject);
	UMovieSceneKzDialogueTrack* DialogueTrack = CastChecked<UMovieSceneKzDialogueTrack>(&Track);
	return MakeShared<FKzDialogueSection>(*DialogueSection, *DialogueTrack);
}

const FSlateBrush* FKzDialogueTrackEditor::GetIconBrush() const
{
	// Reuse a built-in style that reads as "speech / dialogue".
	return FAppStyle::GetBrush("Icons.Comment");
}

// =======================================================================================
// Section view
// =======================================================================================

FKzDialogueSection::FKzDialogueSection(UMovieSceneKzDialogueSection& InSection, UMovieSceneKzDialogueTrack& InTrack)
	: Section(&InSection)
	, Track(&InTrack)
{
}

UMovieSceneSection* FKzDialogueSection::GetSectionObject()
{
	return Section;
}

FText FKzDialogueSection::GetSectionTitle() const
{
	if (!Section) { return FText::GetEmpty(); }

	if (Track && Track->DialogueAsset)
	{
		FKzDialogueLine Line;
		if (Track->DialogueAsset->TryGetLineById(Section->LineId, Line))
		{
			// Scale the truncation length to the section's duration: short sections
			// get a tight title, long sections get more text. This roughly mirrors how
			// much horizontal space the section occupies on the timeline.
			int32 MaxLen = 24; // sensible default if we can't compute duration
			if (UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>())
			{
				const FFrameRate TickResolution = MovieScene->GetTickResolution();
				const TRange<FFrameNumber> Range = Section->GetRange();
				if (Range.HasLowerBound() && Range.HasUpperBound())
				{
					const double DurationSeconds = TickResolution.AsSeconds(FFrameTime(Range.GetUpperBoundValue() - Range.GetLowerBoundValue()));

					// ~10 chars per second feels right at default Sequencer zoom and
					// keeps short barks readable while long lines show enough text.
					constexpr float CharsPerSecond = 10.f;
					constexpr int32 MinChars = 8;
					constexpr int32 MaxChars = 120;

					MaxLen = FMath::Clamp(FMath::RoundToInt(DurationSeconds * CharsPerSecond), MinChars, MaxChars);
				}
			}

			return Line.GetDisplayLabel(MaxLen);
		}
	}
	return FText::FromString(Section->DisplayName);
}

int32 FKzDialogueSection::OnPaintSection(FSequencerSectionPainter& InPainter) const
{
	return InPainter.PaintSectionBackground();
}

#undef LOCTEXT_NAMESPACE