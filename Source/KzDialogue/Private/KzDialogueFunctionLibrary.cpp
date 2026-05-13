// Copyright 2026 kirzo

#include "KzDialogueFunctionLibrary.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"
#include "Engine/World.h"

static UKzDialogueSubsystem* GetDialogueSubsystem(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject)) { return nullptr; }
	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
}

bool UKzDialogueFunctionLibrary::IsDialogueSpeakerValid(const FKzDialogueSpeaker& Speaker)
{
	return Speaker.IsValid();
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub))
	{
		return nullptr;
	}
	return bCreateIfNotFound ? Sub->GetOrCreatePlayer(InChannel) : Sub->FindPlayer(InChannel);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset,
	FGameplayTag Channel, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayAsset(Asset, Channel, bStartImmediately) : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayAssetLine(Asset, LineId, Channel, Priority, bStartImmediately) : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Ref, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	if (!Ref.IsValid()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return nullptr; }

	UKzDialogueAsset* Loaded = Ref.Asset.LoadSynchronous();
	if (!Loaded) { return nullptr; }

	return Sub->PlayAssetLine(Loaded, Ref.LineId, Channel, Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineDirect(const UObject* WorldContextObject, const FKzDialogueLine& Line, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayLine(Line, Channel, Priority, bStartImmediately) : nullptr;
}

bool UKzDialogueFunctionLibrary::TryResolveDialogueLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine)
{
	return Ref.TryResolve(OutLine);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& List, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	if (!List.IsValid()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return nullptr; }

	UKzDialogueAsset* Loaded = List.Asset.LoadSynchronous();
	if (!Loaded) { return nullptr; }

	UKzDialoguePlayer* Player = nullptr;
	for (int32 i = 0; i < List.LineIds.Num(); ++i)
	{
		// First entry: respect bStartImmediately as the caller passed it.
		// Subsequent entries: queue them (bStartImmediately=false) so they run after
		// the previous one finishes on the channel.
		const bool bFirstEntry = (i == 0);
		const bool bThisStartImmediately = bFirstEntry && bStartImmediately;

		UKzDialoguePlayer* Result = Sub->PlayAssetLine(Loaded, List.LineIds[i], Channel, Priority, bThisStartImmediately);
		if (bFirstEntry) { Player = Result; }
	}
	return Player;
}

bool UKzDialogueFunctionLibrary::TryResolveDialogueLineList(const FKzDialogueLineList& List, TArray<FKzDialogueLine>& OutLines)
{
	return List.TryResolveAll(OutLines);
}

void UKzDialogueFunctionLibrary::GetDialogueLineRefsFromList(const FKzDialogueLineList& List, TArray<FKzDialogueLineRef>& OutRefs)
{
	List.GetLineRefs(OutRefs);
}

bool UKzDialogueFunctionLibrary::IsDialogueChannelPlaying(const UObject* WorldContextObject, FGameplayTag Channel)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return false; }

	UKzDialoguePlayer* Player = Sub->FindPlayer(Channel);
	return IsValid(Player) && Player->IsPlaying();
}

void UKzDialogueFunctionLibrary::StopDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->StopChannel(Channel);
	}
}