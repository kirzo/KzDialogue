// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "ScriptableTasks/ScriptableTask.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "ScriptableTask_PlayDialogueAsset.generated.h"

class UKzAsyncPlayDialogueAsset;
class UKzDialogueAsset;
class UKzDialoguePlayer;

/** Plays a whole dialogue asset, line after line, and finishes when it ends or is cut short. Stopping the task stops the dialogue. */
UCLASS(DisplayName = "Play Dialogue Asset", meta = (TaskCategory = "Dialogue"))
class KZDIALOGUESCRIPTABLE_API UScriptableTask_PlayDialogueAsset : public UScriptableTask
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TObjectPtr<UKzDialogueAsset> Asset;

	/** Empty resolves each line's own channel (line, asset, settings). */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** On a per speaker channel, such as Bark, the actor whose player the dialogue plays on, as an NPC for its bubble; on a per listener channel, the listener. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ScriptableContext))
	TObjectPtr<AActor> Owner;

	/** Inherit takes the asset's own setting. */
	UPROPERTY(EditAnywhere, Category = "Dialogue", AdvancedDisplay)
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit;

	virtual bool IsStoppable() const override { return true; }

protected:
	virtual void BeginTask() override;
	virtual void StopTask() override;

private:
	UFUNCTION()
	void HandleEnded(UKzDialoguePlayer* Player, const FKzDialogueLine& EndedLine);

	UPROPERTY(Transient)
	TObjectPtr<UKzAsyncPlayDialogueAsset> Action;
};