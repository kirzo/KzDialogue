// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueNotify.h"
#include "KzDialogueBuiltinNotifies.generated.h"

class UAnimMontage;
class USoundBase;
class UCameraShakeBase;
class UForceFeedbackEffect;

/**
 * Point: plays a montage once on the target speaker's mesh. Fire-and-forget -- the montage is
 * NOT stopped if the line is cut short; use the state variant for a montage bounded to the line.
 */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Play Montage"))
class KZDIALOGUE_API UKzDialogueNotify_PlayMontage : public UKzDialogueNotify
{
	GENERATED_BODY()

public:
	/** Montage to play on the target's skeletal mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	TObjectPtr<UAnimMontage> Montage;

	/** Playback speed multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	float PlayRate = 1.0f;

	/** Optional section to jump to right after starting. None plays from the beginning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	FName StartSection;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "PlayMontage", "Play Montage"); }
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) override;
};

/**
 * State: plays a montage on NotifyBegin and stops it (blended) on NotifyEnd, so it is bounded to
 * the line -- if the line is skipped or interrupted, the flush ends it. Use for held poses or
 * looping idles that must not outlive the line.
 */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Play Montage (State)"))
class KZDIALOGUE_API UKzDialogueNotifyState_PlayMontage : public UKzDialogueNotifyState
{
	GENERATED_BODY()

public:
	/** Montage to play for the duration of the window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	TObjectPtr<UAnimMontage> Montage;

	/** Playback speed multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	float PlayRate = 1.0f;

	/** Blend-out time used when the state ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	float BlendOutTime = 0.25f;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "PlayMontageState", "Play Montage (State)"); }
	virtual void NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context) override;
	virtual void NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context) override;
};

/** Point: plays a one-shot sound attached to the target speaker (or 2D), separate from the line VO. */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Play Sound"))
class KZDIALOGUE_API UKzDialogueNotify_PlaySound : public UKzDialogueNotify
{
	GENERATED_BODY()

public:
	/** Sound to play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TObjectPtr<USoundBase> Sound;

	/** Volume multiplier applied to the spawned sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	float VolumeMultiplier = 1.0f;

	/** Pitch multiplier applied to the spawned sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	float PitchMultiplier = 1.0f;

	/** Socket on the target's mesh to attach to. None attaches to the actor root. Ignored when 2D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FName AttachSocket;

	/** Play as a non-positional 2D sound instead of attaching to the speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	bool bPlay2D = false;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "PlaySound", "Play Sound"); }
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) override;
};

/** Point: plays a camera shake on the local player's camera. */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Camera Shake"))
class KZDIALOGUE_API UKzDialogueNotify_CameraShake : public UKzDialogueNotify
{
	GENERATED_BODY()

public:
	/** Camera shake class to play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	/** Intensity scale passed to the shake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake")
	float Scale = 1.0f;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "CameraShake", "Camera Shake"); }
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) override;
};

/** Point: plays a force feedback (rumble) effect on the local player's controller. */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Force Feedback"))
class KZDIALOGUE_API UKzDialogueNotify_ForceFeedback : public UKzDialogueNotify
{
	GENERATED_BODY()

public:
	/** Force feedback effect to play (non-looping; a point notify has no End to stop a loop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Force Feedback")
	TObjectPtr<UForceFeedbackEffect> Effect;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "ForceFeedback", "Force Feedback"); }
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) override;
};

/**
 * State: applies gameplay tags to the target speaker for the duration of the window. NotifyBegin
 * adds them, NotifyEnd removes them (ref-counted on the speaker, so overlapping notifies stack).
 * The AnimBP / gameplay reads them via UKzDialogueSpeakerComponent::GetActiveDialogueTags to drive
 * mood, expression, etc.
 */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Set Tag"))
class KZDIALOGUE_API UKzDialogueNotifyState_SetTag : public UKzDialogueNotifyState
{
	GENERATED_BODY()

public:
	/** Tags applied to the speaker while this state is active (e.g. Dialogue.Mood.Angry). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag")
	FGameplayTagContainer Tags;

	virtual FText GetNotifyName() const override { return NSLOCTEXT("KzDialogueNotifies", "SetTag", "Set Tag"); }
	virtual void NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context) override;
	virtual void NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context) override;
};
