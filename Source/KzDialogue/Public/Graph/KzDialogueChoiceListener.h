// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "KzDialogueChoiceListener.generated.h"

class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnChoiceListenerConversation, UKzDialogueGraphInstance*, Instance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnChoiceListenerOptions, UKzDialogueGraphInstance*, Instance, const TArray<FKzDialogueGraphOption>&, Options);

/**
 * Learns about the conversations of some channels for one player, and answers them: any widget or
 * actor creates one to show options and choose, without being a choice widget. On a per listener
 * channel it only hears that player's conversations; on the other channels, every one. The subsystem
 * keeps it alive while whoever created it lives, or until Stop Listening.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialogueChoiceListener : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Listens to the dialogue graph plays of Channels, matched hierarchically. Player defaults to the
	 * creating widget's owning player, else the first local player. A conversation already running is
	 * listened to without events: read it with Get Options.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player", Categories = "Dialogue.Channel"))
	static UKzDialogueChoiceListener* CreateDialogueChoiceListener(UObject* WorldContextObject, const TArray<FGameplayTag>& Channels, APlayerController* Player = nullptr);

	/** Listens to one play only, for what does not go through the channels. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices", meta = (WorldContext = "WorldContextObject"))
	static UKzDialogueChoiceListener* ListenToDialogueGraph(UObject* WorldContextObject, UKzDialogueGraphInstance* Instance);

	/** Stops hearing anything; the subsystem lets it go. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void StopListening();

	/** Chooses one of the options shown. False when none are or that option is not available. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	bool ChooseOption(int32 Index);

	/** Advances the line of the conversation, for a "press A to go on" in Manual mode. Nothing while choosing. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void Next();

	/** The options shown; empty when none are. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	TArray<FKzDialogueGraphOption> GetOptions() const;

	/** The conversation Next advances: the last one that started and is still running. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	UKzDialogueGraphInstance* GetInstance() const { return CurrentInstance.Get(); }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	bool IsChoosing() const { return ChoosingInstance.IsValid(); }

	/** Seconds left of the time limit of the options shown, for a countdown; 0 without one. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	float GetTimeLeft() const;

	/** Whoever created it is gone, so nothing is left to tell. */
	bool IsAbandoned() const { return bHasCreator && !Creator.IsValid(); }

	/** A conversation it listens to started: the moment to give the box its input. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceListenerConversation OnConversationStarted;

	/** A conversation it listens to ended. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceListenerConversation OnConversationEnded;

	/** Options to show. Each one's Index is what Choose Option takes; unavailable ones are there only to be shown disabled. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceListenerOptions OnShowOptions;

	/** The options shown are gone: one was chosen, the time ran out, or the conversation ended. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceListenerConversation OnHideOptions;

private:
	static UKzDialogueChoiceListener* Create(UObject* WorldContextObject);

	UFUNCTION()
	void HandleGraphStarted(UKzDialogueGraphInstance* Instance);

	UFUNCTION()
	void HandleOptionsOffered(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options);

	UFUNCTION()
	void HandleOptionsWithdrawn(UKzDialogueGraphInstance* Instance);

	UFUNCTION()
	void HandleGraphEnded(UKzDialogueGraphInstance* Instance, FName Exit, bool bInterrupted);

	/** Whether the play is one of its channels' and, on a per listener channel, its player's. */
	bool Accepts(const UKzDialogueGraphInstance* Instance) const;

	void Listen(UKzDialogueGraphInstance* Instance);
	void Unlisten(UKzDialogueGraphInstance* Instance);

	TArray<FGameplayTag> Channels;
	TWeakObjectPtr<APlayerController> Player;

	/** Whoever created it, which keeps it alive. */
	TWeakObjectPtr<UObject> Creator;
	bool bHasCreator = false;

	/** Listens to one play only, given at creation. */
	bool bSingleInstance = false;

	TArray<TWeakObjectPtr<UKzDialogueGraphInstance>> Instances;
	TWeakObjectPtr<UKzDialogueGraphInstance> CurrentInstance;

	/** The play whose options are shown: the last one that offered. */
	TWeakObjectPtr<UKzDialogueGraphInstance> ChoosingInstance;
};