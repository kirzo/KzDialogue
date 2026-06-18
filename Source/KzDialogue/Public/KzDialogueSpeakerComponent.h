// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
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
class KZDIALOGUE_API UKzDialogueSpeakerComponent : public UActorComponent
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

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	static TMap<FGameplayTag, TWeakObjectPtr<UKzDialogueSpeakerComponent>>& GetRegistry(const UWorld* World);
};