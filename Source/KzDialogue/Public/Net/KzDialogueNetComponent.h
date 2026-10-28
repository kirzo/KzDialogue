// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
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

	/** Asks the server to advance the player of Channel and Owner, as Next on its copy here. */
	UFUNCTION(Server, Reliable)
	void ServerNext(FGameplayTag Channel, AActor* Owner);

	//~ UActorComponent
	virtual void PostInitProperties() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};