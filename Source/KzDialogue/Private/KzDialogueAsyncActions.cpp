// Copyright 2026 kirzo

#include "KzDialogueAsyncActions.h"
#include "KzDialogueAsset.h"
#include "KzDialogueFunctionLibrary.h"
#include "KzDialoguePlayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogueAsync, Log, All);

// ------------------------------------------------------------------------------------------------
// UKzAsyncDialogueAction
// ------------------------------------------------------------------------------------------------

bool UKzAsyncDialogueAction::AcquirePlayer()
{
	UKzDialoguePlayer* Player = UKzDialogueFunctionLibrary::GetDialoguePlayer(WorldContext, Channel, /*bCreateIfNotFound*/ true);
	if (!IsValid(Player))
	{
		UE_LOG(LogKzDialogueAsync, Warning, TEXT("%s: no dialogue player available for channel '%s'."), *GetName(), *Channel.ToString());
		return false;
	}

	DialoguePlayer = Player;
	return true;
}

bool UKzAsyncDialogueAction::BindSpecificLineFinished(const FKzDialogueLineRef& Ref)
{
	if (!DialoguePlayer) return false;

	FKzOnDialogueLineSingleEvent Callback;
	Callback.BindDynamic(this, &UKzAsyncDialogueAction::HandleSpecificLineFinished);

	LineBindingHandle = DialoguePlayer->BindOnSpecificLineFinished(Ref, Callback, /*bAutoUnbind*/ true);
	return LineBindingHandle.IsValid();
}

void UKzAsyncDialogueAction::SubscribeDialogueFinished()
{
	if (DialoguePlayer)
	{
		DialoguePlayer->OnDialogueFinished.AddDynamic(this, &UKzAsyncDialogueAction::HandleDialogueFinished);
	}
}

void UKzAsyncDialogueAction::CleanupBindings()
{
	if (DialoguePlayer)
	{
		DialoguePlayer->UnbindSpecificLine(LineBindingHandle);
		DialoguePlayer->OnDialogueFinished.RemoveAll(this);
	}
	LineBindingHandle.Invalidate();
}

void UKzAsyncDialogueAction::HandleDialogueFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	// Still alive here means the awaited line(s) never completed: the dialogue was stopped,
	// aborted, interrupted, or ran out without playing them.
	NotifyCancelled();
}

void UKzAsyncDialogueAction::SetReadyToDestroy()
{
	CleanupBindings();
	DialoguePlayer = nullptr;
	WorldContext = nullptr;

	Super::SetReadyToDestroy();
}

// ------------------------------------------------------------------------------------------------
// UKzAsyncPlayDialogueLine
// ------------------------------------------------------------------------------------------------

UKzAsyncPlayDialogueLine* UKzAsyncPlayDialogueLine::PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Line, FGameplayTag InChannel, int32 InPriority, bool bInStartImmediately)
{
	UKzAsyncPlayDialogueLine* Action = NewObject<UKzAsyncPlayDialogueLine>();
	Action->WorldContext = const_cast<UObject*>(WorldContextObject);
	Action->LaunchRef = Line;
	Action->Channel = InChannel;
	Action->Priority = InPriority;
	Action->bStartImmediately = bInStartImmediately;
	if (WorldContextObject)
	{
		Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
	}
	return Action;
}

void UKzAsyncPlayDialogueLine::Activate()
{
	Super::Activate();

	if (!WorldContext || !LaunchRef.IsValid() || !AcquirePlayer())
	{
		NotifyCancelled();
		return;
	}

	// Bind BEFORE playing so the line cannot finish in between.
	if (!BindSpecificLineFinished(LaunchRef))
	{
		UE_LOG(LogKzDialogueAsync, Warning, TEXT("%s: line ref does not resolve in '%s'."), *GetName(), *LaunchRef.Asset.ToString());
		NotifyCancelled();
		return;
	}

	UKzDialoguePlayer* Result = UKzDialogueFunctionLibrary::PlayDialogueLine(WorldContext, LaunchRef, Channel, Priority, bStartImmediately);
	if (Result != DialoguePlayer)
	{
		NotifyCancelled();
		return;
	}

	// Watchdog AFTER playing: launching may interrupt previous channel content, which
	// broadcasts OnDialogueFinished during the call above.
	SubscribeDialogueFinished();
	Started.Broadcast(DialoguePlayer, FKzDialogueLine());
}

void UKzAsyncPlayDialogueLine::HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line)
{
	// The binding auto-unbound on dispatch.
	LineBindingHandle.Invalidate();

	Finished.Broadcast(Player, Line);
	SetReadyToDestroy();
}

void UKzAsyncPlayDialogueLine::NotifyCancelled()
{
	Cancelled.Broadcast(DialoguePlayer, FKzDialogueLine());
	SetReadyToDestroy();
}

// ------------------------------------------------------------------------------------------------
// UKzAsyncPlayDialogueLineInline
// ------------------------------------------------------------------------------------------------

UKzAsyncPlayDialogueLineInline* UKzAsyncPlayDialogueLineInline::PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 InPriority, bool bInStartImmediately)
{
	UKzAsyncPlayDialogueLineInline* Action = NewObject<UKzAsyncPlayDialogueLineInline>();
	Action->WorldContext = const_cast<UObject*>(WorldContextObject);
	Action->LaunchRef.Asset = Asset;
	Action->LaunchRef.LineId = LineId;
	Action->Channel = InChannel;
	Action->Priority = InPriority;
	Action->bStartImmediately = bInStartImmediately;
	if (WorldContextObject)
	{
		Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
	}
	return Action;
}

// ------------------------------------------------------------------------------------------------
// UKzAsyncDialogueSequenceAction
// ------------------------------------------------------------------------------------------------

void UKzAsyncDialogueSequenceAction::Activate()
{
	Super::Activate();

	GatherEntryRefs(EntryRefs);
	CurrentIndex = 0;

	if (!WorldContext || EntryRefs.IsEmpty() || !AcquirePlayer())
	{
		NotifyCancelled();
		return;
	}

	// Bind BEFORE playing so the first line cannot finish in between.
	if (!BindNextEntry())
	{
		UE_LOG(LogKzDialogueAsync, Warning, TEXT("%s: no entry of the sequence resolves."), *GetName());
		NotifyCancelled();
		return;
	}

	UKzDialoguePlayer* Result = LaunchPlayback();
	if (Result != DialoguePlayer)
	{
		NotifyCancelled();
		return;
	}

	// Watchdog AFTER playing: launching may interrupt previous channel content, which
	// broadcasts OnDialogueFinished during the call above.
	SubscribeDialogueFinished();
	Started.Broadcast(DialoguePlayer, FKzDialogueLine());
}

bool UKzAsyncDialogueSequenceAction::BindNextEntry()
{
	// Unresolvable entries never play, so they would never fire: skip them like playback does.
	while (CurrentIndex < EntryRefs.Num())
	{
		if (BindSpecificLineFinished(EntryRefs[CurrentIndex]))
		{
			return true;
		}

		UE_LOG(LogKzDialogueAsync, Warning, TEXT("%s: skipping unresolvable sequence entry %d."), *GetName(), CurrentIndex);
		++CurrentIndex;
	}
	return false;
}

void UKzAsyncDialogueSequenceAction::HandleSpecificLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line)
{
	// The binding auto-unbound on dispatch.
	LineBindingHandle.Invalidate();

	LineFinished.Broadcast(Player, Line);

	++CurrentIndex;
	if (CurrentIndex < EntryRefs.Num() && BindNextEntry())
	{
		return;
	}

	// Every playable entry completed (trailing unresolvable entries are skipped, like playback does).
	Finished.Broadcast(Player, Line);
	SetReadyToDestroy();
}

void UKzAsyncDialogueSequenceAction::NotifyCancelled()
{
	Cancelled.Broadcast(DialoguePlayer, FKzDialogueLine());
	SetReadyToDestroy();
}

// ------------------------------------------------------------------------------------------------
// UKzAsyncPlayDialogueLineList
// ------------------------------------------------------------------------------------------------

UKzAsyncPlayDialogueLineList* UKzAsyncPlayDialogueLineList::PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& Lines, FGameplayTag InChannel, int32 InPriority, bool bInStartImmediately)
{
	UKzAsyncPlayDialogueLineList* Action = NewObject<UKzAsyncPlayDialogueLineList>();
	Action->WorldContext = const_cast<UObject*>(WorldContextObject);
	Action->LaunchList = Lines;
	Action->Channel = InChannel;
	Action->Priority = InPriority;
	Action->bStartImmediately = bInStartImmediately;
	if (WorldContextObject)
	{
		Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
	}
	return Action;
}

void UKzAsyncPlayDialogueLineList::GatherEntryRefs(TArray<FKzDialogueLineRef>& OutRefs) const
{
	LaunchList.GetLineRefs(OutRefs);
}

UKzDialoguePlayer* UKzAsyncPlayDialogueLineList::LaunchPlayback()
{
	return UKzDialogueFunctionLibrary::PlayDialogueLineList(WorldContext, LaunchList, Channel, Priority, bStartImmediately);
}

// ------------------------------------------------------------------------------------------------
// UKzAsyncPlayDialogueLineRefs
// ------------------------------------------------------------------------------------------------

UKzAsyncPlayDialogueLineRefs* UKzAsyncPlayDialogueLineRefs::PlayDialogueLineRefs(const UObject* WorldContextObject, const TArray<FKzDialogueLineRef>& Lines, FGameplayTag InChannel, int32 InPriority, bool bInStartImmediately)
{
	UKzAsyncPlayDialogueLineRefs* Action = NewObject<UKzAsyncPlayDialogueLineRefs>();
	Action->WorldContext = const_cast<UObject*>(WorldContextObject);
	Action->LaunchRefs = Lines;
	Action->Channel = InChannel;
	Action->Priority = InPriority;
	Action->bStartImmediately = bInStartImmediately;
	if (WorldContextObject)
	{
		Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
	}
	return Action;
}

void UKzAsyncPlayDialogueLineRefs::GatherEntryRefs(TArray<FKzDialogueLineRef>& OutRefs) const
{
	OutRefs = LaunchRefs;
}

UKzDialoguePlayer* UKzAsyncPlayDialogueLineRefs::LaunchPlayback()
{
	return UKzDialogueFunctionLibrary::PlayDialogueLineRefs(WorldContext, LaunchRefs, Channel, Priority, bStartImmediately);
}