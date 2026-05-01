// Copyright 2026 kirzo

#include "Sequencer/MovieSceneKzDialogueTrack.h"

#include "Sequencer/MovieSceneKzDialogueSection.h"
#include "KzDialogueAsset.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"

#include "Evaluation/MovieSceneEvaluationTemplate.h"
#include "IMovieScenePlayer.h"
#include "MovieScene.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "KzDialogueTrack"

// =======================================================================================
// Section eval template implementation
// =======================================================================================

FMovieSceneKzDialogueSectionTemplate::FMovieSceneKzDialogueSectionTemplate(const UMovieSceneKzDialogueTrack& InTrack, const UMovieSceneKzDialogueSection& InSection)
	: Asset(InTrack.DialogueAsset)
	, Channel(InTrack.Channel)
	, LineId(InSection.LineId)
	, bSuppressAudio(InTrack.bSuppressAudio)
{
	SectionStartFrame = InSection.HasStartFrame() ? InSection.GetInclusiveStartFrame() : 0;

	if (UMovieScene* MovieScene = InTrack.GetTypedOuter<UMovieScene>())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber DurationFrames = InSection.GetExclusiveEndFrame() - SectionStartFrame;
		SectionDurationSeconds = static_cast<float>(TickResolution.AsSeconds(FFrameTime(DurationFrames)));
	}
}

UScriptStruct& FMovieSceneKzDialogueSectionTemplate::GetScriptStructImpl() const
{
	return *StaticStruct();
}

void FMovieSceneKzDialogueSectionTemplate::Setup(FPersistentEvaluationData& /*PersistentData*/, IMovieScenePlayer& /*Player*/) const {}
void FMovieSceneKzDialogueSectionTemplate::TearDown(FPersistentEvaluationData& /*PersistentData*/, IMovieScenePlayer& /*Player*/) const {}

void FMovieSceneKzDialogueSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	// Only fire on the very first evaluation step that includes the section's start
	// (so scrubbing back and forth doesn't replay continuously). The Sequencer engine
	// already handles "section just entered" semantics through the Direction/Status
	// checks in modern UE; we keep it simple.
	if (Context.HasJumped() || Context.GetStatus() != EMovieScenePlayerStatus::Playing)
	{
		return;
	}

	if (!Context.GetRange().Contains(FFrameTime(SectionStartFrame)))
	{
		return;
	}

	// Reach into IMovieScenePlayer via execution tokens isn't possible directly;
	// use ExecutionTokens to enqueue a deferred task that has access to a player.
	struct FToken : IMovieSceneExecutionToken
	{
		TWeakObjectPtr<UKzDialogueAsset> WeakAsset;
		FGameplayTag Channel;
		FGuid LineId;
		bool bSuppressAudio = false;
		float SectionDurationSeconds = 0.0f;

		virtual void Execute(const FMovieSceneContext& InContext, const FMovieSceneEvaluationOperand& InOperand, FPersistentEvaluationData& InPersistentData, IMovieScenePlayer& InPlayer) override
		{
			UObject* PlaybackContextObj = InPlayer.GetPlaybackContext();
			UWorld* World = PlaybackContextObj ? PlaybackContextObj->GetWorld() : nullptr;
			if (!World) { return; }

			UKzDialogueSubsystem* Sub = World->GetSubsystem<UKzDialogueSubsystem>();
			UKzDialogueAsset* AssetPtr = WeakAsset.Get();
			if (!IsValid(Sub) || !IsValid(AssetPtr)) { return; }

			FKzDialogueLine LineCopy;
			if (!AssetPtr->TryGetLineById(LineId, LineCopy)) { return; }

			if (bSuppressAudio)
			{
				LineCopy.Audio = nullptr;
			}

			if (SectionDurationSeconds > 0.0f)
			{
				LineCopy.Duration = SectionDurationSeconds;
			}

			Sub->PlayLine(LineCopy, Channel, /*Priority=*/0);
		}
	};

	FToken Token;
	Token.WeakAsset = Asset;
	Token.Channel = Channel;
	Token.LineId = LineId;
	Token.bSuppressAudio = bSuppressAudio;
	Token.SectionDurationSeconds = SectionDurationSeconds;

	ExecutionTokens.Add(MoveTemp(Token));
}

// =======================================================================================
// UMovieSceneKzDialogueTrack
// =======================================================================================

UMovieSceneKzDialogueTrack::UMovieSceneKzDialogueTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(74, 110, 182, 65);
#endif

	Channel = Kz::Tags::Dialogue::MainChannel;
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);
}

UMovieSceneSection* UMovieSceneKzDialogueTrack::CreateNewSection()
{
	return NewObject<UMovieSceneKzDialogueSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneKzDialogueTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}

void UMovieSceneKzDialogueTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UMovieSceneKzDialogueTrack::RemoveSectionAt(int32 SectionIndex)
{
	if (Sections.IsValidIndex(SectionIndex)) { Sections.RemoveAt(SectionIndex); }
}

bool UMovieSceneKzDialogueTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

bool UMovieSceneKzDialogueTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

const TArray<UMovieSceneSection*>& UMovieSceneKzDialogueTrack::GetAllSections() const
{
	return reinterpret_cast<const TArray<UMovieSceneSection*>&>(Sections);
}

bool UMovieSceneKzDialogueTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneKzDialogueSection::StaticClass();
}

FName UMovieSceneKzDialogueTrack::GetTrackName() const
{
	return TEXT("KzDialogue");
}

FText UMovieSceneKzDialogueTrack::GetDisplayName() const
{
	static const FText DefaultDisplayName = LOCTEXT("TrackDisplayName", "KzDialogue");

	if (DialogueAsset)
	{
		return FText::Format(
			LOCTEXT("DisplayNameWithAsset", "{0} ({1})"),
			DefaultDisplayName,
			FText::FromString(DialogueAsset->GetName())
		);
	}

	return DefaultDisplayName;
}

FMovieSceneEvalTemplatePtr UMovieSceneKzDialogueTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	if (const UMovieSceneKzDialogueSection* DialogueSection = Cast<UMovieSceneKzDialogueSection>(&InSection))
	{
		return FMovieSceneKzDialogueSectionTemplate(*this, *DialogueSection);
	}
	return FMovieSceneEvalTemplatePtr();
}

#undef LOCTEXT_NAMESPACE

// Helpful for the eval template registration in newer UE versions.
void EnsureKzDialogueEvalTemplateLinkerExists() {}