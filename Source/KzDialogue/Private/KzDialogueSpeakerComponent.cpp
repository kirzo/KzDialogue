// Copyright 2026 kirzo

#include "KzDialogueSpeakerComponent.h"

#include "KzDialogueAsset.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueSubsystem.h"
#include "KzSpeakerAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "Engine/World.h"
#include "UObject/ObjectKey.h"

namespace KzDialogueSpeakerInternal
{
	// Per-world, per-speaker registration stack. We key on UWorld* and use weak pointers to the
	// components so that worlds tearing down without explicit unregistration don't keep stale
	// pointers. TObjectKey rather than a raw asset pointer: the static registry outlives worlds
	// and a recycled object slot must not alias a dead speaker. Several components may share a
	// speaker (e.g. a Sequencer spawnable duplicating a level character): the last registered
	// valid one wins lookups, and unregistering removes only that component's own entry, so the
	// previous registrant takes over again.
	static TMap<TWeakObjectPtr<const UWorld>, TMap<TObjectKey<const UKzSpeakerAsset>, TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>>> Registry;
}

UKzDialogueSpeakerComponent::UKzDialogueSpeakerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

USceneComponent* UKzDialogueSpeakerComponent::GetAudioAttachComponent(FName& OutSocketName) const
{
	OutSocketName = NAME_None;

	if (!AudioAttachPoint.ComponentName.IsNone())
	{
		if (USceneComponent* Component = AudioAttachPoint.GetComponent(this))
		{
			OutSocketName = AudioAttachPoint.SocketName;
			return Component;
		}
	}

	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetRootComponent() : nullptr;
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

	if (UWorld* World = GetWorld(); World && (Speaker || ExtraPersonas.Num() > 0))
	{
		auto& WorldMap = KzDialogueSpeakerInternal::Registry.FindOrAdd(World);
		auto RegisterUnder = [this, &WorldMap](const UKzSpeakerAsset* Identity)
		{
			if (!Identity) { return; }
			TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>& Stack = WorldMap.FindOrAdd(TObjectKey<const UKzSpeakerAsset>(Identity));
			Stack.Remove(this); // re-registration moves us to the top
			Stack.Add(this);
		};

		RegisterUnder(Speaker);
		for (const UKzSpeakerAsset* Persona : ExtraPersonas)
		{
			RegisterUnder(Persona);
		}
	}
}

void UKzDialogueSpeakerComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
		{
			auto UnregisterFrom = [this, WorldMap](const UKzSpeakerAsset* Identity)
			{
				if (!Identity) { return; }
				const TObjectKey<const UKzSpeakerAsset> Key(Identity);
				if (TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>* Stack = WorldMap->Find(Key))
				{
					// Remove only our own entry — never evict another registrant sharing the speaker.
					Stack->Remove(this);
					if (Stack->IsEmpty()) { WorldMap->Remove(Key); }
				}
			};

			UnregisterFrom(Speaker);
			for (const UKzSpeakerAsset* Persona : ExtraPersonas)
			{
				UnregisterFrom(Persona);
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

UKzDialogueSpeakerComponent* UKzDialogueSpeakerComponent::FindSpeaker(const UObject* WorldContextObject, const UKzSpeakerAsset* InSpeaker)
{
	if (!IsValid(WorldContextObject) || !InSpeaker) { return nullptr; }

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return nullptr; }

	if (auto* WorldMap = KzDialogueSpeakerInternal::Registry.Find(World))
	{
		if (const TArray<TWeakObjectPtr<UKzDialogueSpeakerComponent>>* Stack = WorldMap->Find(TObjectKey<const UKzSpeakerAsset>(InSpeaker)))
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
		LineCopy.Speaker.Asset = Speaker;
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