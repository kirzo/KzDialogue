// Copyright 2026 kirzo

#include "Net/KzDialogueNetActor.h"
#include "Net/UnrealNetwork.h"

AKzDialogueNetActor::AKzDialogueNetActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	// Changes force their own update; the rate only matters for what never changes.
	SetNetUpdateFrequency(1.0f);
}

void AKzDialogueNetActor::PostInitProperties()
{
	Super::PostInitProperties();
	Entries.Host = this;
}

void AKzDialogueNetActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKzDialogueNetActor, Entries);
}