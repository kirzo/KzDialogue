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

void FMovieSceneKzDialogueSectionTemplate::Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& /*Player*/) const
{
	// Initialize per-section persistent state.
	PersistentData.AddSectionData<FMovieSceneKzDialogueSectionState>();
}

void FMovieSceneKzDialogueSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	// The section is leaving the evaluation field. This happens on Stop, on a Jump
	// that takes us out of the section, on full sequence finish, etc. We want to:
	//   1) Stop any dialogue we triggered (only if it's still ours).
	//   2) Reset our "fired" flag so a fresh Play through this section will work again.

	UObject* PlaybackContextObj = Player.GetPlaybackContext();
	UWorld* World = PlaybackContextObj ? PlaybackContextObj->GetWorld() : nullptr;
	if (UKzDialogueSubsystem* Sub = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr)
	{
		// Only stop if the active line in the channel is ours. Otherwise we'd kill a
		// dialogue triggered by something else that shares the channel.
		if (UKzDialoguePlayer* DialoguePlayer = Sub->FindPlayer(Channel))
		{
			if (DialoguePlayer->IsPlaying() && DialoguePlayer->GetCurrentLine().LineId == LineId)
			{
				Sub->StopChannel(Channel);
			}
		}
	}

	// State is per-section in PersistentData; clearing the flag is enough.
	if (FMovieSceneKzDialogueSectionState* State = PersistentData.FindSectionData<FMovieSceneKzDialogueSectionState>())
	{
		State->bFired = false;
		State->LastStatus = EMovieScenePlayerStatus::Stopped;
	}
}

void FMovieSceneKzDialogueSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& /*Operand*/, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	const EMovieScenePlayerStatus::Type Status = Context.GetStatus();

	struct FToken : IMovieSceneExecutionToken
	{
		// Identity of the section/line we represent.
		TWeakObjectPtr<UKzDialogueAsset> WeakAsset;
		FGameplayTag Channel;
		FGuid LineId;
		bool bSuppressAudio = false;
		float SectionDurationSeconds = 0.0f;

		// What we want the executor to do this evaluation.
		enum class EAction : uint8 { None, FireIfNew, Pause, Resume } Action = EAction::None;

		virtual void Execute(const FMovieSceneContext& InContext, const FMovieSceneEvaluationOperand& InOperand, FPersistentEvaluationData& InPersistentData, IMovieScenePlayer& InPlayer) override
		{
			UObject* PlaybackContextObj = InPlayer.GetPlaybackContext();
			UWorld* World = PlaybackContextObj ? PlaybackContextObj->GetWorld() : nullptr;
			UKzDialogueSubsystem* Sub = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
			if (!IsValid(Sub)) { return; }

			switch (Action)
			{
			case EAction::FireIfNew:
			{
				// Lazily create the per-section state. Setup() isn't always called
				// before Execute in the modern Sequencer evaluation flow, so we can't
				// rely on FindSectionData returning non-null.
				FMovieSceneKzDialogueSectionState& State = InPersistentData.GetOrAddSectionData<FMovieSceneKzDialogueSectionState>();
				if (State.bFired) { return; }

				UKzDialogueAsset* AssetPtr = WeakAsset.Get();
				if (!IsValid(AssetPtr)) { return; }

				FKzDialogueLine LineCopy;
				if (!AssetPtr->TryGetLineById(LineId, LineCopy)) { return; }

				if (bSuppressAudio) { LineCopy.Audio = nullptr; }
				if (SectionDurationSeconds > 0.f) { LineCopy.Duration = SectionDurationSeconds; }

				// PlayLine respects channel and asset interruption rules; if rejected,
				// we still mark as fired so we don't keep retrying every frame the
				// section is active. The caller can re-trigger with Stop+Play.
				Sub->PlayLine(LineCopy, Channel);
				State.bFired = true;
				break;
			}

			case EAction::Pause:
				if (UKzDialoguePlayer* P = Sub->FindPlayer(Channel))
				{
					if (P->IsPlaying() && P->GetCurrentLine().LineId == LineId)
					{
						P->Pause();
					}
				}
				break;

			case EAction::Resume:
				if (UKzDialoguePlayer* P = Sub->FindPlayer(Channel))
				{
					if (P->GetState() == EKzDialogueState::Paused && P->GetCurrentLine().LineId == LineId)
					{
						P->Resume();
					}
				}
				break;

			default:
				break;
			}
		}
	};

	FToken Token;
	Token.WeakAsset = Asset;
	Token.Channel = Channel;
	Token.LineId = LineId;
	Token.bSuppressAudio = bSuppressAudio;
	Token.SectionDurationSeconds = SectionDurationSeconds;

	// Read the cached state to detect transitions. We can't write to it from a const
	// Evaluate, so the actual mutation happens in the StatusUpdateToken's Execute.
	const FMovieSceneKzDialogueSectionState* State = PersistentData.FindSectionData<FMovieSceneKzDialogueSectionState>();
	const EMovieScenePlayerStatus::Type LastStatus = State ? State->LastStatus : EMovieScenePlayerStatus::Stopped;

	switch (Status)
	{
	case EMovieScenePlayerStatus::Playing:
		// First Playing tick after Pause -> resume.
		if (LastStatus == EMovieScenePlayerStatus::Paused)
		{
			Token.Action = FToken::EAction::Resume;
		}
		else
		{
			// Otherwise: try to fire (the executor checks bFired and skips if already done).
			Token.Action = FToken::EAction::FireIfNew;
		}
		break;

	case EMovieScenePlayerStatus::Paused:
		if (LastStatus == EMovieScenePlayerStatus::Playing)
		{
			Token.Action = FToken::EAction::Pause;
		}
		break;

	case EMovieScenePlayerStatus::Jumping:
	case EMovieScenePlayerStatus::Scrubbing:
	case EMovieScenePlayerStatus::Stopped:
	case EMovieScenePlayerStatus::Stepping:
	default:
		// No-op: we don't fire on these states. The dialogue subsystem stays in
		// whatever state it was; if Stop was just hit on the sequence, TearDown will
		// run and stop our line.
		Token.Action = FToken::EAction::None;
		break;
	}

	// Track the status transition. We do this via a side-channel token because
	// PersistentData is const here, but we want to update LastStatus regardless of
	// whether we're firing this evaluation.
	struct FStatusUpdateToken : IMovieSceneExecutionToken
	{
		EMovieScenePlayerStatus::Type NewStatus = EMovieScenePlayerStatus::Stopped;
		virtual void Execute(const FMovieSceneContext&, const FMovieSceneEvaluationOperand&,
			FPersistentEvaluationData& InPersistentData, IMovieScenePlayer&) override
		{
			// Lazily create the state — same reason as in FToken::Execute.
			FMovieSceneKzDialogueSectionState& State = InPersistentData.GetOrAddSectionData<FMovieSceneKzDialogueSectionState>();
			State.LastStatus = NewStatus;
		}
	};
	FStatusUpdateToken StatusToken;
	StatusToken.NewStatus = Status;
	ExecutionTokens.Add(MoveTemp(StatusToken));

	// Then the actual action token (executed after, but in practice they commute since
	// LastStatus is read in this Evaluate from the prior frame's snapshot).
	if (Token.Action != FToken::EAction::None)
	{
		ExecutionTokens.Add(MoveTemp(Token));
	}
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

#if WITH_EDITORONLY_DATA
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
#endif

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