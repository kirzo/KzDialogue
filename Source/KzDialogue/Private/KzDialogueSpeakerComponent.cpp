// Copyright 2026 kirzo

#include "KzDialogueSpeakerComponent.h"

#include "KzDialogueAsset.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueSubsystem.h"
#include "Settings/KzDialogueSettings.h"

#include "Engine/World.h"

namespace KzDialogueSpeakerInternal
{
	// Per-world, per-tag registration stack. We key on UWorld* and use weak pointers to the
	// components so that worlds tearing down without explicit unregistration don't keep stale
	// pointers. Several components may share a tag (e.g. a Sequencer spawnable duplicating a
	// level character): the last registered valid one wins lookups, and unregistering removes
	// only that component's own entry, so the previous registrant takes over again.
	static TMap<TWeakObjectPtr<const UWorld>, TMap<FGameplayTag, TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>>> Registry;
}

UKzDialogueSpeakerComponent::UKzDialogueSpeakerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

FKzSpeakingLevelSettings UKzDialogueSpeakerComponent::ResolveSpeakingSettings() const
{
	if (bOverrideSpeakingSettings)
	{
		return SpeakingSettings;
	}
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	return Settings ? Settings->SpeakingDefaults : FKzSpeakingLevelSettings();
}

void UKzDialogueSpeakerComponent::PushSpeakingSuppression()
{
	const bool bWasSuppressed = SpeakingSuppressionCount > 0;
	++SpeakingSuppressionCount;
	if (!bWasSuppressed)
	{
		OnSpeakingSuppressedChanged.Broadcast(true); // 0 -> 1
	}
}

void UKzDialogueSpeakerComponent::PopSpeakingSuppression()
{
	if (SpeakingSuppressionCount <= 0)
	{
		return; // unbalanced Pop — nothing to release
	}
	--SpeakingSuppressionCount;
	if (SpeakingSuppressionCount == 0)
	{
		OnSpeakingSuppressedChanged.Broadcast(false); // 1 -> 0
	}
}

void UKzDialogueSpeakerComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld(); World && SpeakerTag.IsValid())
	{
		TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>& Stack = KzDialogueSpeakerInternal::Registry.FindOrAdd(World).FindOrAdd(SpeakerTag);
		Stack.Remove(this); // re-registration moves us to the top
		Stack.Add(this);
	}
}

void UKzDialogueSpeakerComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
		{
			if (TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>* Stack = WorldMap->Find(SpeakerTag))
			{
				// Remove only our own entry — never evict another registrant sharing the tag.
				Stack->Remove(this);
				if (Stack->IsEmpty()) { WorldMap->Remove(SpeakerTag); }
			}
		}
	}
	Super::OnUnregister();
}

void UKzDialogueSpeakerComponent::SetSpeakingLevel(float NewLevel)
{
	if (!FMath::IsNearlyEqual(NewLevel, SpeakingLevel))
	{
		SpeakingLevel = NewLevel;
		OnSpeakingLevelChanged.Broadcast(SpeakingLevel);
	}
}

UKzDialogueSpeakerComponent* UKzDialogueSpeakerComponent::FindSpeakerByTag(const UObject* WorldContextObject, FGameplayTag InSpeakerTag)
{
	if (!IsValid(WorldContextObject) || !InSpeakerTag.IsValid()) { return nullptr; }

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return nullptr; }

	if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
	{
		if (const TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>* Stack = WorldMap->Find(InSpeakerTag))
		{
			// Latest valid registrant wins; stale weak entries are skipped.
			for (int32 Index = Stack->Num() - 1; Index >= 0; --Index)
			{
				if (UKzDialogueSpeakerComponent* Speaker = (*Stack)[Index].Get())
				{
					return Speaker;
				}
			}
		}
	}
	return nullptr;
}

UKzDialogueAssetSession* UKzDialogueSpeakerComponent::Speak(UKzDialogueAsset* Asset)
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

void UKzDialogueSpeakerComponent::AddDialogueTags(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		int32& Count = ActiveDialogueTagCounts.FindOrAdd(Tag);
		if (Count++ == 0)
		{
			ActiveDialogueTags.AddTag(Tag);
		}
	}
}

void UKzDialogueSpeakerComponent::RemoveDialogueTags(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		int32* Count = ActiveDialogueTagCounts.Find(Tag);
		if (Count && --(*Count) <= 0)
		{
			ActiveDialogueTagCounts.Remove(Tag);
			ActiveDialogueTags.RemoveTag(Tag);
		}
	}
}

void UKzDialogueSpeakerComponent::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();
	TagContainer.AppendTags(ActiveDialogueTags);
}

bool UKzDialogueSpeakerComponent::HasMatchingGameplayTag(FGameplayTag TagToCheck) const
{
	return ActiveDialogueTags.HasTag(TagToCheck);
}

bool UKzDialogueSpeakerComponent::HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	return ActiveDialogueTags.HasAll(TagContainer);
}

bool UKzDialogueSpeakerComponent::HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	return ActiveDialogueTags.HasAny(TagContainer);
}