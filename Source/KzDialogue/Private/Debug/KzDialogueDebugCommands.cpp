// Copyright 2026 kirzo

#include "Debug/KzDialogueDebugCommands.h"
#include "KzDialogue.h"

#if !UE_BUILD_SHIPPING

#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "KzDialogueSpeakerComponent.h"
#include "KzSpeakerAsset.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"

namespace KzDialogueCmd
{
	// ===================================================================================
	// World / subsystem resolution
	// ===================================================================================

	/**
	 * Resolve the most relevant active world: PIE first, then Game, then any. Logs a
	 * warning if nothing is found.
	 */
	static UWorld* ResolveActiveWorld()
	{
		if (!GEngine) { return nullptr; }

		// Prefer PIE. UWorld::Type covers both PIE clients and standalone game worlds.
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.WorldType == EWorldType::PIE)
			{
				return Context.World();
			}
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.WorldType == EWorldType::Game)
			{
				return Context.World();
			}
		}
		// Fallback: anything with a world.
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World()) { return Context.World(); }
		}
		return nullptr;
	}

	static UKzDialogueSubsystem* ResolveSubsystem()
	{
		UWorld* World = ResolveActiveWorld();
		return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	}

	// ===================================================================================
	// Argument helpers
	// ===================================================================================

	static FGameplayTag ParseChannelArg(const TArray<FString>& Args, int32 Index)
	{
		if (!Args.IsValidIndex(Index)) { return FGameplayTag(); }
		return FGameplayTag::RequestGameplayTag(FName(*Args[Index]), /*ErrorIfNotFound=*/false);
	}

	static void LogToConsole(const FString& Message)
	{
		UE_LOG(LogKzDialogue, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.0f, FColor::Cyan, Message);
		}
	}

	// ===================================================================================
	// Command handlers
	// ===================================================================================

	static void HandlePlay(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			LogToConsole(TEXT("Usage: Kz.Dialogue.Play <AssetPath> [ChannelTag]"));
			return;
		}

		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub)
		{
			LogToConsole(TEXT("No active dialogue subsystem (no PIE/Game world)."));
			return;
		}

		UKzDialogueAsset* Asset = LoadObject<UKzDialogueAsset>(nullptr, *Args[0]);
		if (!Asset)
		{
			LogToConsole(FString::Printf(TEXT("Asset not found: %s"), *Args[0]));
			return;
		}

		const FGameplayTag Channel = ParseChannelArg(Args, 1);
		const UKzDialogueAssetSession* Session = Sub->PlayAsset(Asset, Channel);
		LogToConsole(Session && Session->IsPlaying()
			? FString::Printf(TEXT("Playing '%s' on channel '%s'"), *Asset->GetName(), *Session->GetChannel().ToString())
			: FString::Printf(TEXT("Failed to play '%s' (rejected by priority?)"), *Asset->GetName()));
	}

	static void HandleStop(const TArray<FString>& Args)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 0);
		if (Channel.IsValid())
		{
			Sub->StopChannel(Channel);
			LogToConsole(FString::Printf(TEXT("Stopped channel '%s'"), *Channel.ToString()));
		}
		else
		{
			Sub->StopAll();
			LogToConsole(TEXT("Stopped all channels"));
		}
	}

	/** Run a player command on the channel argument, or on the default channel. */
	static void RunOnPlayer(const TArray<FString>& Args, const TCHAR* Done, void (UKzDialoguePlayer::*Command)())
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 0);
		if (UKzDialoguePlayer* Player = Sub->FindPlayer(Channel.IsValid() ? Channel : Sub->DefaultChannel))
		{
			(Player->*Command)();
			LogToConsole(FString::Printf(TEXT("%s channel '%s'"), Done, *Player->Channel.ToString()));
		}
	}

	static void HandleNext(const TArray<FString>& Args) { RunOnPlayer(Args, TEXT("Advanced"), &UKzDialoguePlayer::Next); }
	static void HandlePause(const TArray<FString>& Args) { RunOnPlayer(Args, TEXT("Paused"), &UKzDialoguePlayer::Pause); }
	static void HandleResume(const TArray<FString>& Args) { RunOnPlayer(Args, TEXT("Resumed"), &UKzDialoguePlayer::Resume); }
	static void HandleAbort(const TArray<FString>& Args) { RunOnPlayer(Args, TEXT("Aborted"), &UKzDialoguePlayer::Abort); }
	static void HandleInterrupt(const TArray<FString>& Args) { RunOnPlayer(Args, TEXT("Interrupted"), &UKzDialoguePlayer::Interrupt); }

	static void HandleSetSpeed(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			LogToConsole(TEXT("Usage: Kz.Dialogue.SetSpeed <factor> [ChannelTag]"));
			return;
		}

		const float Factor = FCString::Atof(*Args[0]);
		if (Factor <= 0.f)
		{
			LogToConsole(TEXT("Speed factor must be > 0"));
			return;
		}

		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 1);
		if (Channel.IsValid())
		{
			if (UKzDialoguePlayer* Player = Sub->FindPlayer(Channel))
			{
				Player->TimeScale = Factor;
				LogToConsole(FString::Printf(TEXT("TimeScale = %.2f on channel '%s'"), Factor, *Channel.ToString()));
			}
		}
		else
		{
			TArray<UKzDialoguePlayer*> Players;
			Sub->GetAllPlayers(Players);
			for (UKzDialoguePlayer* Player : Players)
			{
				Player->TimeScale = Factor;
			}
			LogToConsole(FString::Printf(TEXT("TimeScale = %.2f on %d player(s)"), Factor, Players.Num()));
		}
	}

	static void HandleListChannels(const TArray<FString>& /*Args*/)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		TArray<UKzDialoguePlayer*> Active;
		Sub->GetAllPlayers(Active);

		if (Active.Num() == 0)
		{
			LogToConsole(TEXT("No active dialogue channels."));
			return;
		}

		LogToConsole(FString::Printf(TEXT("=== %d channel(s) ==="), Active.Num()));
		for (const UKzDialoguePlayer* Player : Active)
		{
			if (!Player) { continue; }
			const FString StateName = StaticEnum<EKzDialogueState>()
				? StaticEnum<EKzDialogueState>()->GetNameStringByValue((int64)Player->GetState())
				: TEXT("?");
			LogToConsole(FString::Printf(
				TEXT("  %s  [%s]  pri=%d  scale=%.2f"),
				*Player->Channel.ToString(),
				*StateName,
				Player->CurrentPriority,
				Player->TimeScale));
		}
	}

	static void HandleListSpeakers(const TArray<FString>& /*Args*/)
	{
		UWorld* World = ResolveActiveWorld();
		if (!World) { return; }

		TMap<const UKzSpeakerAsset*, UKzDialogueSpeakerComponent*> Speakers;
		UKzDialogueSpeakerComponent::GetRegisteredSpeakers(World, Speakers);

		if (Speakers.Num() == 0)
		{
			LogToConsole(TEXT("No speakers registered in this world."));
			return;
		}

		LogToConsole(FString::Printf(TEXT("=== %d speaker(s) ==="), Speakers.Num()));
		for (const TPair<const UKzSpeakerAsset*, UKzDialogueSpeakerComponent*>& Pair : Speakers)
		{
			LogToConsole(FString::Printf(TEXT("  %s%s  on %s"), *Pair.Key->GetName(), Pair.Value->Speaker != Pair.Key ? TEXT(" (persona)") : TEXT(""), *GetNameSafe(Pair.Value->GetOwner())));
		}
	}

	static void HandleStatus(const TArray<FString>& Args)
	{
		HandleListChannels(Args);
		HandleListSpeakers(Args);
	}

	static void HandleResetAliasStates(const TArray<FString>& /*Args*/)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		Sub->ResetAllAliasStates();
		LogToConsole(TEXT("KzDialogue: cleared all alias playback states."));
	}

	static void HandleResetAliasState(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			LogToConsole(TEXT("Usage: Kz.Dialogue.ResetAliasState <AliasGuid>"));
			return;
		}

		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		FGuid AliasId;
		if (!FGuid::Parse(Args[0], AliasId))
		{
			LogToConsole(FString::Printf(TEXT("Invalid GUID: %s"), *Args[0]));
			return;
		}

		Sub->ResetAliasState(AliasId);
		LogToConsole(FString::Printf(TEXT("KzDialogue: reset alias state %s."),
			*AliasId.ToString(EGuidFormats::Digits)));
	}
}

// =======================================================================================
// Registration
// =======================================================================================

FKzDialogueDebugCommands::FKzDialogueDebugCommands()
{
	using namespace KzDialogueCmd;

	const auto Make = [this](const TCHAR* Name, const TCHAR* Help,
		void (*Handler)(const TArray<FString>&))
		{
			Commands.Add(MakeUnique<FAutoConsoleCommand>(
				Name, Help,
				FConsoleCommandWithArgsDelegate::CreateLambda(
					[Handler](const TArray<FString>& Args) { Handler(Args); })));
		};

	Make(TEXT("Kz.Dialogue.Play"), TEXT("Play <AssetPath> [Channel]"), HandlePlay);
	Make(TEXT("Kz.Dialogue.Stop"), TEXT("Stop [Channel] (no channel = all)"), HandleStop);
	Make(TEXT("Kz.Dialogue.Next"), TEXT("Next [Channel]"), HandleNext);
	Make(TEXT("Kz.Dialogue.Pause"), TEXT("Pause [Channel]"), HandlePause);
	Make(TEXT("Kz.Dialogue.Resume"), TEXT("Resume [Channel]"), HandleResume);
	Make(TEXT("Kz.Dialogue.Abort"), TEXT("Abort [Channel]: hard stop, no exit phases"), HandleAbort);
	Make(TEXT("Kz.Dialogue.Interrupt"), TEXT("Interrupt [Channel]: hard stop with a short audio fade"), HandleInterrupt);
	Make(TEXT("Kz.Dialogue.SetSpeed"), TEXT("SetSpeed <factor> [Channel]"), HandleSetSpeed);
	Make(TEXT("Kz.Dialogue.ListChannels"), TEXT("List active channels and their state"), HandleListChannels);
	Make(TEXT("Kz.Dialogue.ListSpeakers"), TEXT("List registered speaker identities (personas included) in the active world"), HandleListSpeakers);
	Make(TEXT("Kz.Dialogue.Status"), TEXT("Dump full dialogue state"), HandleStatus);
	Make(TEXT("Kz.Dialogue.ResetAliasStates"), TEXT("Reset all cached alias playback states"), HandleResetAliasStates);
	Make(TEXT("Kz.Dialogue.ResetAliasState"), TEXT("ResetAliasState <AliasGuid>: reset cached state for a single alias"), HandleResetAliasState);
}

FKzDialogueDebugCommands::~FKzDialogueDebugCommands()
{
	// FAutoConsoleCommand RAII handles unregistration on destruction.
	Commands.Reset();
}

#endif // !UE_BUILD_SHIPPING