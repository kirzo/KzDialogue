// Copyright 2026 kirzo

#include "KzDialogueAsyncActions.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "KzDialogueFunctionLibrary.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogueAsync, Log, All);

// ------------------------------------------------------------------------------------------------
// UKzAsyncDialogueAction
// ------------------------------------------------------------------------------------------------

bool UKzAsyncDialogueAction::AcquirePlayer()
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogKzDialogueAsync, Warning, TEXT("%s: no dialogue subsystem available."), *GetName());
		return false;
	}

	// Resolve the definitive channel up-front so the player bound here is the one playback
	// lands on; LaunchPlayback then passes the resolved (explicit) channel through.
	Channel = ResolveLaunchChannel(*Subsystem);

	UKzDialoguePlayer* Player = Subsystem->GetOrCreatePlayer(Channel);
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

void UKzAsyncDialogueAction::Stop()
{
	if (!DialoguePlayer) return;

	// Unbind first so the player's OnDialogueFinished (fired by Stop) doesn't also route through the
	// watchdog; then resolve as cancelled ourselves.
	UKzDialoguePlayer* Player = DialoguePlayer;
	CleanupBindings();
	Player->Stop();
	NotifyCancelled();
}

void UKzAsyncDialogueAction::Interrupt()
{
	if (!DialoguePlayer) return;

	UKzDialoguePlayer* Player = DialoguePlayer;
	CleanupBindings();
	Player->Interrupt();
	NotifyCancelled();
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

FGameplayTag UKzAsyncPlayDialogueLine::ResolveLaunchChannel(const UKzDialogueSubsystem& Subsystem) const
{
	return Subsystem.ResolveChannelForEntry(Channel, LaunchRef.Asset.LoadSynchronous(), LaunchRef.LineId);
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

FGameplayTag UKzAsyncDialogueSequenceAction::ResolveLaunchChannel(const UKzDialogueSubsystem& Subsystem) const
{
	// First-entry rule, matching the subsystem's sequence play paths.
	if (EntryRefs.IsEmpty())
	{
		return Channel;
	}
	return Subsystem.ResolveChannelForEntry(Channel, EntryRefs[0].Asset.LoadSynchronous(), EntryRefs[0].LineId);
}

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

// ------------------------------------------------------------------------------------------------
// UKzAsyncPlayDialogueAsset
// ------------------------------------------------------------------------------------------------

UKzAsyncPlayDialogueAsset* UKzAsyncPlayDialogueAsset::PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* InAsset, FGameplayTag InChannel, bool bInStartImmediately, EKzDialogueAdvanceMode InAdvanceMode)
{
	UKzAsyncPlayDialogueAsset* Action = NewObject<UKzAsyncPlayDialogueAsset>();
	Action->WorldContext = const_cast<UObject*>(WorldContextObject);
	Action->Asset = InAsset;
	Action->Channel = InChannel;
	Action->bStartImmediately = bInStartImmediately;
	Action->AdvanceMode = InAdvanceMode;
	if (WorldContextObject)
	{
		Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
	}
	return Action;
}

void UKzAsyncPlayDialogueAsset::Activate()
{
	Super::Activate();

	if (!WorldContext || !Asset)
	{
		NotifyCancelled();
		return;
	}

	// The session resolves each line's channel and chains runs across channel changes, finishing ONCE
	// for the whole asset. We wait on it instead of a single channel player.
	Session = UKzDialogueFunctionLibrary::PlayDialogueAsset(WorldContext, Asset, Channel, bStartImmediately, AdvanceMode);
	if (!Session)
	{
		NotifyCancelled();
		return;
	}

	if (!Session->IsPlaying())
	{
		// Finished within the play call (empty asset, or the first run was refused). Resolve with the
		// session's recorded reason instead of waiting for a finish event that already fired.
		HandleAssetFinished(Session->GetCurrentPlayer(), Session->GetFinishReason());
		return;
	}

	// Bind completion AFTER playing: launching may interrupt previous channel content, which broadcasts
	// the session's OnDialogueFinished during the play call above.
	Session->OnDialogueFinished.AddDynamic(this, &UKzAsyncPlayDialogueAsset::HandleAssetFinished);
	Started.Broadcast(Session->GetCurrentPlayer(), FKzDialogueLine());
}

void UKzAsyncPlayDialogueAsset::HandleAssetFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (Reason == EKzDialogueFinishReason::Completed)
	{
		Finished.Broadcast(Player, FKzDialogueLine());
	}
	else
	{
		Cancelled.Broadcast(Player, FKzDialogueLine());
	}
	SetReadyToDestroy();
}

void UKzAsyncPlayDialogueAsset::Stop()
{
	if (!Session) { return; }

	// Unbind first so the session's finish (fired by Stop) doesn't also route through HandleAssetFinished;
	// then resolve as cancelled ourselves.
	Session->OnDialogueFinished.RemoveAll(this);
	Session->Stop();
	NotifyCancelled();
}

void UKzAsyncPlayDialogueAsset::Interrupt()
{
	if (!Session) { return; }

	Session->OnDialogueFinished.RemoveAll(this);
	Session->Interrupt();
	NotifyCancelled();
}

void UKzAsyncPlayDialogueAsset::SetReadyToDestroy()
{
	if (Session)
	{
		Session->OnDialogueFinished.RemoveAll(this);
		Session = nullptr;
	}
	Super::SetReadyToDestroy();
}

void UKzAsyncPlayDialogueAsset::NotifyCancelled()
{
	Cancelled.Broadcast(Session ? Session->GetCurrentPlayer() : nullptr, FKzDialogueLine());
	SetReadyToDestroy();
}