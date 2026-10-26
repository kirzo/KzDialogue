// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "KzChoiceWidget.generated.h"

/** Shows the options of the dialogue graphs played on its channels. The Blueprint lays them out in On Show Options and answers with Choose Option. */
UCLASS(Abstract)
class KZDIALOGUE_API UKzChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Chooses one of the options shown. False when none are shown or that option is not available. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	bool ChooseOption(int32 Index);

	/** The play whose options are shown; null when none are. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	UKzDialogueGraphInstance* GetShownInstance() const { return Shown.Get(); }

protected:
	/** Channels whose plays it shows, matched hierarchically. Empty shows every channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Choices", meta = (Categories = "Dialogue.Channel"))
	TArray<FGameplayTag> ListenedChannels;

	/** Options to show. Each one's Index is what Choose Option takes; unavailable ones are there only to be shown disabled. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Show Options"))
	void ReceiveShowOptions(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options);

	/** The options shown are gone: one was chosen, the time ran out, or the play ended. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Hide Options"))
	void ReceiveHideOptions(UKzDialogueGraphInstance* Instance);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleGraphStarted(UKzDialogueGraphInstance* Instance);

	UFUNCTION()
	void HandleOptionsOffered(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options);

	UFUNCTION()
	void HandleOptionsWithdrawn(UKzDialogueGraphInstance* Instance);

	UFUNCTION()
	void HandleGraphEnded(UKzDialogueGraphInstance* Instance, FName Exit, bool bInterrupted);

	void Listen(UKzDialogueGraphInstance* Instance);

	TArray<TWeakObjectPtr<UKzDialogueGraphInstance>> Instances;
	TWeakObjectPtr<UKzDialogueGraphInstance> Shown;
};