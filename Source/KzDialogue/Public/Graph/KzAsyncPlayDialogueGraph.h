// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "KzAsyncPlayDialogueGraph.generated.h"

class UKzDialogueGraph;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FKzAsyncDialogueGraphEvent, UKzDialogueGraphInstance*, Instance, FGameplayTag, Event, FName, Exit);

/** Plays a dialogue graph and reports its events and its end. The instance answers choices (Select Option) and stops the play. */
UCLASS()
class KZDIALOGUE_API UKzAsyncPlayDialogueGraph : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true", DisplayName = "Play Dialogue Graph", AutoCreateRefTerm = "Context", AdvancedDisplay = "Context,Entry"))
	static UKzAsyncPlayDialogueGraph* PlayDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context, FName Entry = TEXT("Start"));

	/** Fired before anything plays, with the instance to keep. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueGraphEvent Started;

	/** A Choice offers its options: read them with Get Options, answer with Select Option. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueGraphEvent OptionsOffered;

	/** An Event node fired. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueGraphEvent EventFired;

	/** The play ended through an Exit, with its name (none without one). */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueGraphEvent Finished;

	/** The play was stopped or interrupted, its channel refused it, or the graph has no such entry. */
	UPROPERTY(BlueprintAssignable)
	FKzAsyncDialogueGraphEvent Cancelled;

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleOptions(UKzDialogueGraphInstance* InInstance, const TArray<FKzDialogueGraphOption>& Options);

	UFUNCTION()
	void HandleEvent(UKzDialogueGraphInstance* InInstance, FGameplayTag Event);

	UFUNCTION()
	void HandleEnded(UKzDialogueGraphInstance* InInstance, FName Exit, bool bInterrupted);

	UPROPERTY()
	TObjectPtr<UKzDialogueGraphInstance> Instance;

	FName Entry;
	bool bEnded = false;
};