// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSpeakerComponent.generated.h"

class UKzDialogueAsset;
class UKzDialoguePlayer;

/**
 * Attach to any actor that can speak. Provides:
 *   - A SpeakerTag the dialogue system uses to identify "this actor is the speaker"
 *   - Speak() helpers that spawn a provider rooted to this actor
 *   - Display name resolution
 *   - A registry so other systems can find speakers by tag
 */
UCLASS(ClassGroup = (KzGameplay), Blueprintable, meta = (BlueprintSpawnableComponent))
class KZDIALOGUE_API UKzDialogueSpeakerComponent : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UKzDialogueSpeakerComponent();

	/** Logical identity of this speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	FGameplayTag SpeakerTag;

	/** Display name shown in subtitles (unless overridden by the line). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	FText DisplayName;

	/** Default channel for this speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	FGameplayTag DefaultChannel;

	/** Get the speaker component matching a tag in the given world. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker", meta = (WorldContext = "WorldContextObject"))
	static UKzDialogueSpeakerComponent* FindSpeakerByTag(const UObject* WorldContextObject, FGameplayTag InSpeakerTag);

	/** Speak an asset on this speaker's default channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker")
	UKzDialoguePlayer* Speak(UKzDialogueAsset* Asset);

	/** Speak a single ad-hoc line, as given. Notifies fire only if Line was resolved from an asset;
	 *  a hand-built line has no Timeline (timelines live on the asset keyed by LineId). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker")
	UKzDialoguePlayer* SpeakLine(const FKzDialogueLine& Line);

	/** Add gameplay tags to this speaker for as long as a dialogue notify holds them (ref-counted,
	 *  so overlapping notifies stack). The applied tags are exposed through IGameplayTagAssetInterface
	 *  so the AnimBP / gameplay can query the speaker generically, like an AbilitySystemComponent. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker")
	void AddDialogueTags(const FGameplayTagContainer& Tags);

	/** Release tags previously added by AddDialogueTags. A tag clears once its count reaches zero. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker")
	void RemoveDialogueTags(const FGameplayTagContainer& Tags);

	//~ Begin IGameplayTagAssetInterface
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const override;
	virtual bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
	virtual bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
	//~ End IGameplayTagAssetInterface

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	static TMap<FGameplayTag, TWeakObjectPtr<UKzDialogueSpeakerComponent>>& GetRegistry(const UWorld* World);

	/** Ref-count per tag applied by active dialogue notifies. Runtime state, not serialized. */
	TMap<FGameplayTag, int32> ActiveDialogueTagCounts;

	/** Explicit set mirroring ActiveDialogueTagCounts (keys with count > 0), for fast reads. */
	FGameplayTagContainer ActiveDialogueTags;
};