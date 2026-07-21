// Copyright 2026 kirzo

#include "KzDialogueBuiltinNotifies.h"

#include "KzDialogueSpeakerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace KzDialogueNotifyInternal
{
	// A speaker is usually a Character (use its skeletal mesh); fall back to the first skeletal
	// mesh component for plain actors.
	static USkeletalMeshComponent* ResolveTargetMesh(AActor* Actor)
	{
		if (!Actor) { return nullptr; }
		if (const ACharacter* Char = Cast<ACharacter>(Actor))
		{
			return Char->GetMesh();
		}
		return Actor->FindComponentByClass<USkeletalMeshComponent>();
	}

	static UAnimInstance* ResolveTargetAnimInstance(AActor* Actor)
	{
		const USkeletalMeshComponent* Mesh = ResolveTargetMesh(Actor);
		return Mesh ? Mesh->GetAnimInstance() : nullptr;
	}
}

void UKzDialogueNotify_PlayMontage::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Montage) { return; }

	UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor);
	if (!Anim) { return; }

	Anim->Montage_Play(Montage, PlayRate);
	if (!StartSection.IsNone())
	{
		Anim->Montage_JumpToSection(StartSection, Montage);
	}
}

void UKzDialogueNotifyState_PlayMontage::NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Montage) { return; }
	if (UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor))
	{
		Anim->Montage_Play(Montage, PlayRate);
	}
}

void UKzDialogueNotifyState_PlayMontage::NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Montage) { return; }
	if (UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor))
	{
		Anim->Montage_Stop(BlendOutTime, Montage);
	}
}

void UKzDialogueNotify_PlaySlotAnimation::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Animation) { return; }
	if (UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor))
	{
		Anim->PlaySlotAnimationAsDynamicMontage(Animation, SlotName, BlendIn, BlendOut, PlayRate, NumLoops > 0 ? NumLoops : MAX_int32);
	}
}

void UKzDialogueNotifyState_PlaySlotAnimation::NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Animation) { return; }
	UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor);
	if (!Anim) { return; }

	// Loop just enough to cover the event window; NotifyEnd stops the slot precisely at the end.
	const float WindowLength = FMath::Max(0.f, Context.EventEnd - Context.EventStart);
	const float Rate = FMath::Max(PlayRate, UE_KINDA_SMALL_NUMBER);
	const float EffectiveLength = Animation->GetPlayLength() / Rate;
	const int32 LoopCount = (EffectiveLength > UE_KINDA_SMALL_NUMBER && WindowLength > EffectiveLength) ? FMath::CeilToInt(WindowLength / EffectiveLength) : 1;
	Anim->PlaySlotAnimationAsDynamicMontage(Animation, SlotName, BlendIn, BlendOut, PlayRate, LoopCount);
}

void UKzDialogueNotifyState_PlaySlotAnimation::NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (UAnimInstance* Anim = KzDialogueNotifyInternal::ResolveTargetAnimInstance(Context.TargetActor))
	{
		Anim->StopSlotAnimation(BlendOut, SlotName);
	}
}

namespace KzDialogueNotifyInternal
{
	// SpawnSound2D marks the component as a UI sound. Clear it in game worlds so the SFX pauses
	// with SetGamePaused, but keep it in the editor world (Sequencer preview), where non-UI sounds
	// don't play at all (game not ticking). The flag is re-consulted after start, so post-Play is fine.
	static void MakePausableWithGame(UAudioComponent* Audio)
	{
		if (!Audio) { return; }
		const UWorld* World = Audio->GetWorld();
		Audio->bIsUISound = !(World && World->IsGameWorld());
	}
}

void UKzDialogueNotify_PlaySound::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Sound) { return; }

	if (bPlay2D)
	{
		KzDialogueNotifyInternal::MakePausableWithGame(UGameplayStatics::SpawnSound2D(this, Sound, VolumeMultiplier, PitchMultiplier));
		return;
	}

	USceneComponent* AttachTo = KzDialogueNotifyInternal::ResolveTargetMesh(Context.TargetActor);
	if (!AttachTo && Context.TargetActor)
	{
		AttachTo = Context.TargetActor->GetRootComponent();
	}

	if (AttachTo)
	{
		UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, AttachSocket, FVector::ZeroVector, EAttachLocation::SnapToTarget, false, VolumeMultiplier, PitchMultiplier);
	}
	else
	{
		KzDialogueNotifyInternal::MakePausableWithGame(UGameplayStatics::SpawnSound2D(this, Sound, VolumeMultiplier, PitchMultiplier));
	}
}

void UKzDialogueNotify_CameraShake::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!ShakeClass) { return; }

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraShake(ShakeClass, Scale);
	}
}

void UKzDialogueNotify_ForceFeedback::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Effect) { return; }

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FForceFeedbackParameters Params;
		Params.bLooping = false;
		PC->ClientPlayForceFeedback(Effect, Params);
	}
}

void UKzDialogueNotifyState_SetTag::NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (Tags.IsEmpty()) { return; }
	if (UKzDialogueSpeakerComponent* Speaker = Context.TargetSpeaker)
	{
		Speaker->AddDialogueTags(Tags);
	}
}

void UKzDialogueNotifyState_SetTag::NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (Tags.IsEmpty()) { return; }
	if (UKzDialogueSpeakerComponent* Speaker = Context.TargetSpeaker)
	{
		Speaker->RemoveDialogueTags(Tags);
	}
}

#if WITH_EDITOR
void UKzDialogueNotify_PlayMontage::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Montage) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "MontageUnset", "Montage is not set.")); }
}

void UKzDialogueNotifyState_PlayMontage::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Montage) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "MontageUnset", "Montage is not set.")); }
}

void UKzDialogueNotify_PlaySlotAnimation::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Animation) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "SlotAnimUnset", "Animation is not set.")); }
}

void UKzDialogueNotifyState_PlaySlotAnimation::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Animation) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "SlotAnimUnset", "Animation is not set.")); }
}

void UKzDialogueNotify_PlaySound::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Sound) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "SoundUnset", "Sound is not set.")); }
}

void UKzDialogueNotify_CameraShake::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!ShakeClass) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "ShakeUnset", "Camera shake class is not set.")); }
}

void UKzDialogueNotify_ForceFeedback::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (!Effect) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "EffectUnset", "Force feedback effect is not set.")); }
}

void UKzDialogueNotifyState_SetTag::ValidateNotify(TArray<FText>& OutErrors) const
{
	if (Tags.IsEmpty()) { OutErrors.Add(NSLOCTEXT("KzDialogueNotifies", "TagsUnset", "No tags set.")); }
}
#endif
