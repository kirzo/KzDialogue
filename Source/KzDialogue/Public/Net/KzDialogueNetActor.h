// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/KzDialogueNetTypes.h"
#include "KzDialogueNetActor.generated.h"

/** Carries the server's shared dialogue players (global and per speaker channels) to every client. The dialogue subsystem spawns it on a server when it first needs it. */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class KZDIALOGUE_API AKzDialogueNetActor : public AActor
{
	GENERATED_BODY()

public:
	AKzDialogueNetActor();

	UPROPERTY(Replicated)
	FKzDialogueNetEntries Entries;

	//~ AActor
	virtual void PostInitProperties() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};