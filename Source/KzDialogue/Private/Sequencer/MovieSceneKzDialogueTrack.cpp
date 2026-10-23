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

namespace
{
	// The channel the line lands on, resolved like the play path so firing, pausing and teardown agree on one player.
	FGameplayTag ResolveSectionChannel(IMovieScenePlayer& Player, const UKzDialogueAsset* Asset, FGameplayTag Channel, const FGuid& LineId, UKzDialogueSubsystem*& OutSubsystem)
	{
		UObject* PlaybackContext = Player.GetPlaybackContext();
		UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;
		OutSubsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
		return OutSubsystem ? OutSubsystem->ResolveChannelForEntry(Channel, Asset, LineId) : FGameplayTag();
	}

	// Ours-only guard: never touch a dialogue something else put on the shared channel.
	bool IsOwnLine(const UKzDialoguePlayer* DialoguePlayer, const FGuid& LineId)
	{
		return DialoguePlayer && DialoguePlayer->IsPlaying() && DialoguePlayer->GetCurrentLineId() == LineId;
	}
}

FMovieSceneKzDialogueSectionTemplate::FMovieSceneKzDialogueSectionTemplate(const UMovieSceneKzDialogueTrack& InTrack, const UMovieSceneKzDialogueSection& InSection)
	: Asset(InTrack.DialogueAsset)
	, Channel(InTrack.Channel)
	, LineId(InSection.LineId)
	, bSuppressAudio(InTrack.bSuppressAudio)
{
	const FFrameNumber SectionStartFrame = InSection.HasStartFrame() ? InSection.GetInclusiveStartFrame() : 0;
	SectionEndFrame = InSection.HasEndFrame() ? InSection.GetExclusiveEndFrame() : 0;

	if (UMovieScene* MovieScene = InTrack.GetTypedOuter<UMovieScene>())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber DurationFrames = SectionEndFrame - SectionStartFrame;
		SectionDurationSeconds = static_cast<float>(TickResolution.AsSeconds(FFrameTime(DurationFrames)));

		// Generous margin over a frame delta (hitches included): evaluations closer than this to
		// the section end count as having reached it.
		NaturalEndThresholdFrames = TickResolution.AsFrameNumber(0.5);
	}
}

UScriptStruct& FMovieSceneKzDialogueSectionTemplate::GetScriptStructImpl() const
{
	return *StaticStruct();
}

void FMovieSceneKzDialogueSectionTemplate::Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& /*Player*/) const
{
	PersistentData.AddSectionData<FMovieSceneKzDialogueSectionState>();
}

void FMovieSceneKzDialogueSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	// The section leaves the evaluation field: natural flow past its end, Stop, a jump out
	// (GoToEndAndStop tears down while Playing) or the sequence finishing.
	FMovieSceneKzDialogueSectionState* State = PersistentData.FindSectionData<FMovieSceneKzDialogueSectionState>();

	UKzDialogueSubsystem* Sub = nullptr;
	const FGameplayTag EffectiveChannel = ResolveSectionChannel(Player, Asset, Channel, LineId, Sub);
	if (UKzDialoguePlayer* DialoguePlayer = Sub ? Sub->FindPlayer(EffectiveChannel) : nullptr)
	{
		// Only the last evaluated time tells a cut from completion: an explicit Stop and a natural
		// finish both tear down as Stopped.
		const bool bNaturalFlowPast = State && (SectionEndFrame - State->LastEvalTime.FrameNumber) <= NaturalEndThresholdFrames;

		if (IsOwnLine(DialoguePlayer, LineId))
		{
			// At the end the line just ends, with its fades and audio tail policy, and a next section
			// hot-swapping onto the channel still plays. A cut silences our line and any tails at once.
			if (bNaturalFlowPast) { DialoguePlayer->Next(); }
			else { DialoguePlayer->Interrupt(); }
		}
		else if (!DialoguePlayer->IsPlaying() && State && State->bFired)
		{
			// Our line already completed, but released Continue-policy tails may still ring.
			DialoguePlayer->StopAllAudio();
		}
	}

	if (State)
	{
		State->bFired = false;
		State->bPausedLine = false;
	}
}

void FMovieSceneKzDialogueSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& /*Operand*/, const FMovieSceneContext& Context, const FPersistentEvaluationData& /*PersistentData*/, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	struct FToken : IMovieSceneExecutionToken
	{
		TWeakObjectPtr<UKzDialogueAsset> WeakAsset;
		FGameplayTag Channel;
		FGuid LineId;
		bool bSuppressAudio = false;
		float SectionDurationSeconds = 0.0f;
		EMovieScenePlayerStatus::Type Status = EMovieScenePlayerStatus::Stopped;
		FFrameTime EvalTime;

		virtual void Execute(const FMovieSceneContext& InContext, const FMovieSceneEvaluationOperand& InOperand, FPersistentEvaluationData& InPersistentData, IMovieScenePlayer& InPlayer) override
		{
			// Setup() is not always called before Execute in the modern evaluation flow.
			FMovieSceneKzDialogueSectionState& State = InPersistentData.GetOrAddSectionData<FMovieSceneKzDialogueSectionState>();
			State.LastEvalTime = EvalTime;

			UKzDialogueSubsystem* Sub = nullptr;
			const FGameplayTag EffectiveChannel = ResolveSectionChannel(InPlayer, WeakAsset.Get(), Channel, LineId, Sub);
			if (!Sub) { return; }
			UKzDialoguePlayer* DialoguePlayer = Sub->FindPlayer(EffectiveChannel);

			if (Status == EMovieScenePlayerStatus::Playing)
			{
				if (State.bPausedLine)
				{
					State.bPausedLine = false;
					if (IsOwnLine(DialoguePlayer, LineId)) { DialoguePlayer->Resume(); }
				}
				else if (!State.bFired)
				{
					// Marked fired even when the channel refuses the line, or it would retry every frame.
					State.bFired = true;

					FKzDialogueLine LineCopy;
					UKzDialogueAsset* AssetPtr = WeakAsset.Get();
					if (IsValid(AssetPtr) && AssetPtr->TryGetLineById(LineId, LineCopy))
					{
						if (bSuppressAudio) { LineCopy.Audio = nullptr; }
						if (SectionDurationSeconds > 0.f) { LineCopy.Duration = SectionDurationSeconds; }
						// The sequence owns the timing: a line that would wait for its channel is dropped, not played late.
						if (Sub->PlayLine(LineCopy, EffectiveChannel) && Sub->GetLastPlayResult() == EKzDialoguePlayResult::Queued)
						{
							Sub->CancelQueuedRequest(Sub->GetLastQueuedRequestId());
						}
					}
				}
			}
			else if ((Status == EMovieScenePlayerStatus::Stopped || Status == EMovieScenePlayerStatus::Paused) && State.bFired && !State.bPausedLine && IsOwnLine(DialoguePlayer, LineId))
			{
				// Pausing a sequence evaluates it as Stopped with the section still in the field.
				DialoguePlayer->Pause();
				State.bPausedLine = true;
			}
		}
	};

	FToken Token;
	Token.WeakAsset = Asset;
	Token.Channel = Channel;
	Token.LineId = LineId;
	Token.bSuppressAudio = bSuppressAudio;
	Token.SectionDurationSeconds = SectionDurationSeconds;
	Token.Status = Context.GetStatus();
	Token.EvalTime = Context.GetTime();
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

#if WITH_EDITORONLY_DATA
FText UMovieSceneKzDialogueTrack::GetDisplayName() const
{
	static const FText DefaultDisplayName = LOCTEXT("TrackDisplayName", "Dialogue");

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