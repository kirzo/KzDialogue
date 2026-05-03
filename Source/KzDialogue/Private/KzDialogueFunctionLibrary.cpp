// Copyright 2026 kirzo

#include "KzDialogueFunctionLibrary.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"

static UKzDialogueSubsystem* GetDialogueSubsystem(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject)) { return nullptr; }
	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub))
	{
		return nullptr;
	}
	return bCreateIfNotFound ? Sub->GetOrCreatePlayer(InChannel) : Sub->FindPlayer(InChannel);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset,
	FGameplayTag Channel, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayAsset(Asset, Channel, bStartImmediately) : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLine& Line,
	FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayLine(Line, Channel, Priority, bStartImmediately) : nullptr;
}

void UKzDialogueFunctionLibrary::StopDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->StopChannel(Channel);
	}
}