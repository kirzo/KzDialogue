// Copyright 2026 kirzo

#include "Net/KzDialogueNetComponent.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

UKzDialogueNetComponent::UKzDialogueNetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKzDialogueNetComponent::PostInitProperties()
{
	Super::PostInitProperties();
	Entries.Host = this;
}

void UKzDialogueNetComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UKzDialogueNetComponent, Entries);
}

void UKzDialogueNetComponent::ServerNext_Implementation(FGameplayTag Channel, AActor* Owner)
{
	if (UKzDialogueSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UKzDialogueSubsystem>() : nullptr)
	{
		Subsystem->HandleNetNext(Cast<APlayerController>(GetOwner()), Channel, Owner);
	}
}