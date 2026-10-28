// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "Net/KzDialogueNetTypes.h"
#include "KzDialogueNetComponent.generated.h"

/**
 * Carries one player's own dialogue players (per listener channels) to that player, as its player controller
 * replicates only to it, and that player's requests to the server. The dialogue subsystem adds it to every
 * player controller on a server.
 */
UCLASS(NotBlueprintable)
class KZDIALOGUE_API UKzDialogueNetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKzDialogueNetComponent();

	UPROPERTY(Replicated)
	FKzDialogueNetEntries Entries;

	/** On the server, the owning player's reading speed scale as their machine sent it; 0 until it does. */
	float ReadingSpeedScale = 0.0f;

	/** Asks the server to advance the player of Channel and Owner, as Next on its copy here. */
	UFUNCTION(Server, Reliable)
	void ServerNext(FGameplayTag Channel, AActor* Owner);

	/** Tells the server the copy of the player of Channel and Owner finished a view phase here (its fades), which the player's own dialogue waits for. */
	UFUNCTION(Server, Reliable)
	void ServerViewPhaseDone(FGameplayTag Channel, AActor* Owner, EKzDialogueState Phase, int32 DialogueSerial, int32 LineSerial);

	UFUNCTION(Server, Reliable)
	void ServerSetReadingSpeedScale(float Scale);

	//~ UActorComponent
	virtual void PostInitProperties() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void SendReadingSpeedScale();

	FDelegateHandle ReadingSpeedScaleHandle;
};