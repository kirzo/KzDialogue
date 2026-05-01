// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "GameplayTagContainer.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "MovieSceneKzDialogueTrack.generated.h"

class UKzDialogueAsset;
class UMovieSceneKzDialogueSection;

/**
 * Per-section Sequencer evaluation template. On entering, dispatches the line to the
 * dialogue subsystem; ignored when scrubbing or evaluating in the editor unless the
 * game world is active.
 */
USTRUCT()
struct FMovieSceneKzDialogueSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FMovieSceneKzDialogueSectionTemplate() = default;
	FMovieSceneKzDialogueSectionTemplate(const UMovieSceneKzDialogueTrack& InTrack, const UMovieSceneKzDialogueSection& InSection);

	UPROPERTY() TObjectPtr<UKzDialogueAsset> Asset = nullptr;
	UPROPERTY() FGameplayTag Channel;
	UPROPERTY() FGuid LineId;
	UPROPERTY() bool bSuppressAudio = false;
	UPROPERTY() FFrameNumber SectionStartFrame;
	UPROPERTY() float SectionDurationSeconds = 0.0f;

	virtual UScriptStruct& GetScriptStructImpl() const override;
	virtual void Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
};

/**
 * Sequencer track that drives KzDialogue lines from a UKzDialogueAsset.
 * Sections within this track each play one specific line.
 */
UCLASS(MinimalAPI, DisplayName = "Kz Dialogue")
class UMovieSceneKzDialogueTrack : public UMovieSceneTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()

public:
	UMovieSceneKzDialogueTrack(const FObjectInitializer& ObjectInitializer);

	/** Asset that supplies the available lines to drop as sections. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TObjectPtr<UKzDialogueAsset> DialogueAsset;

	/** Channel the lines will be pushed onto. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FGameplayTag Channel;

	/**
	 * When true and a line has audio, the audio is suppressed (Sequencer is expected
	 * to drive its own audio track, or the cinematic itself plays the dialogue).
	 */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	bool bSuppressAudio = false;

	//~ UMovieSceneTrack
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual bool SupportsMultipleRows() const override { return true; }

	virtual FName GetTrackName() const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const override;
#endif

	//~ IMovieSceneTrackTemplateProducer
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};