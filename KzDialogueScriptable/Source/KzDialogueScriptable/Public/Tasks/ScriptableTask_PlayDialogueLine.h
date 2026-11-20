// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "ScriptableTasks/ScriptableTask.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "ScriptableTask_PlayDialogueLine.generated.h"

class UKzAsyncPlayDialogueLine;
class UKzDialoguePlayer;

/** Plays a line, or one variant of an alias, and finishes when it ends or is cut short. Stopping the task stops the line. */
UCLASS(DisplayName = "Play Dialogue Line", meta = (TaskCategory = "Dialogue"))
class KZDIALOGUESCRIPTABLE_API UScriptableTask_PlayDialogueLine : public UScriptableTask
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FKzDialogueLineRef Line;

	/** Empty resolves the line's own channel (line, asset, settings). */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** On a per speaker channel, such as Bark, the actor whose player the line plays on, as an NPC for its bubble; on a per listener channel, the listener. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ScriptableContext))
	TObjectPtr<AActor> Owner;

	/** -1 inherits the asset's priority, then the channel's. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", AdvancedDisplay)
	int32 Priority = -1;

	virtual bool IsStoppable() const override { return true; }

protected:
	virtual void BeginTask() override;
	virtual void StopTask() override;

private:
	UFUNCTION()
	void HandleEnded(UKzDialoguePlayer* Player, const FKzDialogueLine& EndedLine);

	UPROPERTY(Transient)
	TObjectPtr<UKzAsyncPlayDialogueLine> Action;
};