// Copyright 2026 kirzo

#include "Net/KzDialogueNetComponent.h"
#include "KzDialogueSubsystem.h"
#include "Settings/KzDialogueSettings.h"
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

void UKzDialogueNetComponent::BeginPlay()
{
	Super::BeginPlay();

	// The owning player's machine tells the server how fast its player reads, now and on every change.
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->IsLocalController() && GetNetMode() == NM_Client)
	{
		ReadingSpeedScaleHandle = UKzDialogueSettings::OnReadingSpeedScaleChanged().AddUObject(this, &UKzDialogueNetComponent::SendReadingSpeedScale);
		SendReadingSpeedScale();
	}
}

void UKzDialogueNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UKzDialogueSettings::OnReadingSpeedScaleChanged().Remove(ReadingSpeedScaleHandle);
	Super::EndPlay(EndPlayReason);
}

void UKzDialogueNetComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UKzDialogueNetComponent, Entries);
}

void UKzDialogueNetComponent::SendReadingSpeedScale()
{
	ServerSetReadingSpeedScale(UKzDialogueSettings::GetReadingSpeedScale());
}

void UKzDialogueNetComponent::ServerNext_Implementation(FGameplayTag Channel, AActor* Owner)
{
	if (UKzDialogueSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UKzDialogueSubsystem>() : nullptr)
	{
		Subsystem->HandleNetNext(Cast<APlayerController>(GetOwner()), Channel, Owner);
	}
}

void UKzDialogueNetComponent::ServerViewPhaseDone_Implementation(FGameplayTag Channel, AActor* Owner, EKzDialogueState Phase, int32 DialogueSerial, int32 LineSerial)
{
	if (UKzDialogueSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UKzDialogueSubsystem>() : nullptr)
	{
		Subsystem->HandleNetViewPhaseDone(Cast<APlayerController>(GetOwner()), Channel, Owner, Phase, DialogueSerial, LineSerial);
	}
}

void UKzDialogueNetComponent::ServerSetReadingSpeedScale_Implementation(float Scale)
{
	ReadingSpeedScale = FMath::Clamp(Scale, 0.1f, 10.0f);
}