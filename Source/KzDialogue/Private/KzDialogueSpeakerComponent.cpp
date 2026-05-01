// Copyright 2026 kirzo

#include "KzDialogueSpeakerComponent.h"

#include "KzDialogueAsset.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueSubsystem.h"

#include "Engine/World.h"

namespace KzDialogueSpeakerInternal
{
	// Per-world map. We key on UWorld* and use weak pointers to the components so that
	// worlds tearing down without explicit unregistration don't keep stale pointers.
	static TMap<TWeakObjectPtr<const UWorld>, TMap<FGameplayTag, TWeakObjectPtr<UKzDialogueSpeakerComponent>>> Registry;
}

UKzDialogueSpeakerComponent::UKzDialogueSpeakerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UKzDialogueSpeakerComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld(); World && SpeakerTag.IsValid())
	{
		auto& WorldMap = KzDialogueSpeakerInternal::Registry.FindOrAdd(World);
		WorldMap.Add(SpeakerTag, this);
	}
}

void UKzDialogueSpeakerComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
		{
			WorldMap->Remove(SpeakerTag);
		}
	}
	Super::OnUnregister();
}

UKzDialogueSpeakerComponent* UKzDialogueSpeakerComponent::FindSpeakerByTag(const UObject* WorldContextObject, FGameplayTag InSpeakerTag)
{
	if (!IsValid(WorldContextObject) || !InSpeakerTag.IsValid()) { return nullptr; }

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return nullptr; }

	if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
	{
		if (auto* Weak = WorldMap->Find(InSpeakerTag))
		{
			return Weak->Get();
		}
	}
	return nullptr;
}

TMap<FGameplayTag, TWeakObjectPtr<UKzDialogueSpeakerComponent>>& UKzDialogueSpeakerComponent::GetRegistry(const UWorld* World)
{
	return KzDialogueSpeakerInternal::Registry.FindOrAdd(World);
}

UKzDialoguePlayer* UKzDialogueSpeakerComponent::Speak(UKzDialogueAsset* Asset)
{
	if (!IsValid(Asset) || !GetWorld()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetWorld()->GetSubsystem<UKzDialogueSubsystem>();
	if (!IsValid(Sub)) { return nullptr; }

	return Sub->PlayAsset(Asset, DefaultChannel);
}

UKzDialoguePlayer* UKzDialogueSpeakerComponent::SpeakLine(const FKzDialogueLine& Line)
{
	if (!GetWorld()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetWorld()->GetSubsystem<UKzDialogueSubsystem>();
	if (!IsValid(Sub)) { return nullptr; }

	// If the line doesn't specify its own speaker, inject ours.
	FKzDialogueLine LineCopy = Line;
	if (!LineCopy.Speaker.IsValid())
	{
		LineCopy.Speaker.SpeakerTag = SpeakerTag;
		LineCopy.Speaker.DisplayNameOverride = DisplayName;
	}
	return Sub->PlayLine(LineCopy, DefaultChannel);
}