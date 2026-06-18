// Copyright 2026 kirzo

#include "KzDialogueBuiltinNotifies.h"

#include "KzDialogueSpeakerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
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

void UKzDialogueNotify_PlaySound::Notify_Implementation(const FKzDialogueNotifyContext& Context)
{
	if (!Sound) { return; }

	if (bPlay2D)
	{
		UGameplayStatics::SpawnSound2D(this, Sound, VolumeMultiplier, PitchMultiplier);
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
		UGameplayStatics::SpawnSound2D(this, Sound, VolumeMultiplier, PitchMultiplier);
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
