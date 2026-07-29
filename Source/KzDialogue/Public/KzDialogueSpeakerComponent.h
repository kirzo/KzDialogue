// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/KzComponentReference.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSpeakerComponent.generated.h"

class UKzDialogueAsset;
class UKzDialoguePlayer;
class UKzDialogueAssetSession;
class UKzSpeakerAsset;
class USceneComponent;

/** Fired when a speaker's lip-sync suppression flips on (true = became suppressed) or off (false). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnSpeakingSuppressedChanged, bool, bSuppressed);

/**
 * Attach to any actor that can speak. Provides:
 *   - The speaker asset identifying "this actor is that character"
 *   - Speak() helpers that spawn a provider rooted to this actor
 *   - A registry so other systems can find the world component for a speaker asset
 */
UCLASS(ClassGroup = (KzGameplay), Blueprintable, meta = (BlueprintSpawnableComponent))
class KZDIALOGUE_API UKzDialogueSpeakerComponent : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UKzDialogueSpeakerComponent();

	/** Character this actor speaks as. Injected into speakerless lines by SpeakLine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	TObjectPtr<UKzSpeakerAsset> Speaker;

	/** Additional identities this actor also answers to (hidden persona before a reveal, disguises). Lines referencing any of them resolve to this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	TArray<TObjectPtr<UKzSpeakerAsset>> ExtraPersonas;

	/** Default channel for this speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker")
	FGameplayTag DefaultChannel;

	/** Where attached line audio anchors: a component of the owner plus an optional socket (e.g. the character mesh's head). Unset or unresolvable falls back to the owner's root. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dialogue|Speaker", meta = (NoOffset = "true"))
	FKzComponentSocketReference AudioAttachPoint;

	/** Attach point for this speaker's line audio: the resolved AudioAttachPoint, or the owner's root (OutSocketName = None then). */
	USceneComponent* GetAudioAttachComponent(FName& OutSocketName) const;

	/** Override the project's default speaking-level tuning (gain/threshold/attack/release) for this speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker|Speaking")
	bool bOverrideSpeakingSettings = false;

	/** Per-speaker speaking-level tuning, used when bOverrideSpeakingSettings is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Speaker|Speaking", meta = (EditCondition = "bOverrideSpeakingSettings"))
	FKzSpeakingLevelSettings SpeakingSettings;

	/** Effective speaking tuning: this speaker's override if enabled, else the project default. */
	FKzSpeakingLevelSettings ResolveSpeakingSettings() const;

	/** Fired as this speaker's talking amplitude changes (0..1). Non-zero only while this speaker is the one talking. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Speaker")
	FKzOnSpeakingLevelChanged OnSpeakingLevelChanged;

	/** Smoothed 0..1 talking amplitude (jaw / lip-flap) for THIS speaker; the player gates it to the active speaker. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker")
	float GetSpeakingLevel() const { return IsSpeakingSuppressed() ? 0.0f : SpeakingLevel; }

	/** Set by the dialogue player; routes the active line's speaking amplitude to its speaker. */
	void SetSpeakingLevel(float NewLevel);

	/** Suppress the dialogue-driven speaking level (lip-sync) while something else drives the face (e.g. a Sequencer animation). Ref-counted: balance each Push with a Pop. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker|Speaking")
	void PushSpeakingSuppression();

	/** Release one suppression added by PushSpeakingSuppression. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker|Speaking")
	void PopSpeakingSuppression();

	/** True while the speaking level is suppressed — the face ABP should gate its mouth curve OFF so the other animation wins. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker|Speaking")
	bool IsSpeakingSuppressed() const { return SpeakingSuppressionCount > 0; }

	/** Fired when suppression flips on (true) or off (false). Only the 0<->1 transitions fire, not intermediate Push/Pop. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Speaker|Speaking")
	FKzOnSpeakingSuppressedChanged OnSpeakingSuppressedChanged;

	/** Get the world component registered for a speaker asset (main identity or extra persona). */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Speaker", meta = (WorldContext = "WorldContextObject"))
	static UKzDialogueSpeakerComponent* FindSpeaker(const UObject* WorldContextObject, const UKzSpeakerAsset* InSpeaker);

	/** Speak an asset on this speaker's default channel. Returns the session for the whole asset. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Speaker")
	UKzDialogueAssetSession* Speak(UKzDialogueAsset* Asset);

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
	/** Talking amplitude for this speaker; written by the active dialogue player, read by the face / anim. */
	float SpeakingLevel = 0.0f;

	/** Ref-count of active speaking-level suppressions (Sequencer / animation overrides). */
	int32 SpeakingSuppressionCount = 0;

	/** Ref-count per tag applied by active dialogue notifies. Runtime state, not serialized. */
	TMap<FGameplayTag, int32> ActiveDialogueTagCounts;

	/** Explicit set mirroring ActiveDialogueTagCounts (keys with count > 0), for fast reads. */
	FGameplayTagContainer ActiveDialogueTags;
};