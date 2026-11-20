// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "ScriptableTasks/ScriptableTask.h"
#include "GameplayTagContainer.h"
#include "ScriptableTask_PlayDialogueGraph.generated.h"

class UKzDialogueGraph;
class UKzDialogueGraphInstance;

/** Plays a dialogue graph and finishes when it ends, through an Exit or cut short. Stopping the task stops the play. */
UCLASS(DisplayName = "Play Dialogue Graph", meta = (TaskCategory = "Dialogue"))
class KZDIALOGUESCRIPTABLE_API UScriptableTask_PlayDialogueGraph : public UScriptableTask
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TObjectPtr<UKzDialogueGraph> Graph;

	/** The entry it starts from. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FName Entry = TEXT("Start");

	/** Who starts the dialogue, such as the player talking to an NPC (an interaction's Instigator). Conditions check its tags. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ScriptableContext))
	TObjectPtr<AActor> Instigator;

	/** Who the dialogue belongs to, such as the NPC talked to (an interaction's Owner). */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ScriptableContext))
	TObjectPtr<AActor> Owner;

	/** Empty takes the graph's channel. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", AdvancedDisplay, meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	virtual bool IsStoppable() const override { return true; }

protected:
	virtual void BeginTask() override;
	virtual void StopTask() override;

private:
	UFUNCTION()
	void HandleEnded(UKzDialogueGraphInstance* Ended, FName Exit, bool bInterrupted);

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueGraphInstance> Instance;
};