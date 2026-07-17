// Copyright 2026 kirzo

#include "KzDialogueNotify.h"
#include "KzDialoguePlayer.h"

FText UKzDialogueNotifyBase::GetNotifyName() const
{
	FString RawName = GetClass()->GetName();
	RawName.RemoveFromEnd(TEXT("_C"));
	return FText::FromString(FName::NameToDisplayString(RawName, false));
}

void UKzDialogueNotifyBase::SetOwningPlayer(UKzDialoguePlayer* InPlayer)
{
	OwningPlayer = InPlayer;
}

UWorld* UKzDialogueNotifyBase::GetWorld() const
{
	// Resolve through the driving player (set at bake time). Null on the CDO / before playback,
	// which is the expected "no world" answer.
	if (const UKzDialoguePlayer* Player = OwningPlayer.Get())
	{
		return Player->GetWorld();
	}
	return nullptr;
}