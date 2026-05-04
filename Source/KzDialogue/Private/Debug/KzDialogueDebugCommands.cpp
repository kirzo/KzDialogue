// Copyright 2026 kirzo

#include "Debug/KzDialogueDebugCommands.h"

#if !UE_BUILD_SHIPPING

#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"
#include "KzDialogueSpeakerComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogueCmd, Log, All);

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
		UE_LOG(LogKzDialogueCmd, Display, TEXT("%s"), *Message);
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
		const UKzDialoguePlayer* Player = Sub->PlayAsset(Asset, Channel);
		LogToConsole(Player
			? FString::Printf(TEXT("Playing '%s' on channel '%s'"), *Asset->GetName(), *Player->Channel.ToString())
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

	static void HandleSkip(const TArray<FString>& Args)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 0);
		if (UKzDialoguePlayer* Player = Sub->FindPlayer(Channel.IsValid() ? Channel : Sub->DefaultChannel))
		{
			Player->Skip();
			LogToConsole(FString::Printf(TEXT("Skipped on channel '%s'"), *Player->Channel.ToString()));
		}
	}

	static void HandlePause(const TArray<FString>& Args)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 0);
		if (UKzDialoguePlayer* Player = Sub->FindPlayer(Channel.IsValid() ? Channel : Sub->DefaultChannel))
		{
			Player->Pause();
			LogToConsole(FString::Printf(TEXT("Paused channel '%s'"), *Player->Channel.ToString()));
		}
	}

	static void HandleResume(const TArray<FString>& Args)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		const FGameplayTag Channel = ParseChannelArg(Args, 0);
		if (UKzDialoguePlayer* Player = Sub->FindPlayer(Channel.IsValid() ? Channel : Sub->DefaultChannel))
		{
			Player->Resume();
			LogToConsole(FString::Printf(TEXT("Resumed channel '%s'"), *Player->Channel.ToString()));
		}
	}

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
			// Apply to every existing player.
			int32 Count = 0;
			// Iterating Players directly requires friend access; we use FindPlayer per
			// known channel from settings instead. Cheap enough.
			for (const FGameplayTag& Tag : { Sub->DefaultChannel })
			{
				if (UKzDialoguePlayer* Player = Sub->FindPlayer(Tag))
				{
					Player->TimeScale = Factor;
					++Count;
				}
			}
			LogToConsole(FString::Printf(TEXT("TimeScale = %.2f on %d player(s)"), Factor, Count));
		}
	}

	static void HandleListChannels(const TArray<FString>& /*Args*/)
	{
		UKzDialogueSubsystem* Sub = ResolveSubsystem();
		if (!Sub) { return; }

		// We don't have a public iterator for Players; expose one via a debug-only API.
		// (See note below — small addition to the subsystem header.)
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
		// Speaker registry is per-world; we ask the world's speaker components directly.
		UWorld* World = ResolveActiveWorld();
		if (!World) { return; }

		TArray<UKzDialogueSpeakerComponent*> Speakers;
		for (TObjectIterator<UKzDialogueSpeakerComponent> It; It; ++It)
		{
			if (It->GetWorld() == World) { Speakers.Add(*It); }
		}

		if (Speakers.Num() == 0)
		{
			LogToConsole(TEXT("No speakers registered in this world."));
			return;
		}

		LogToConsole(FString::Printf(TEXT("=== %d speaker(s) ==="), Speakers.Num()));
		for (const UKzDialogueSpeakerComponent* Speaker : Speakers)
		{
			LogToConsole(FString::Printf(
				TEXT("  %s  on %s"),
				*Speaker->SpeakerTag.ToString(),
				Speaker->GetOwner() ? *Speaker->GetOwner()->GetName() : TEXT("<no owner>")));
		}
	}

	static void HandleStatus(const TArray<FString>& Args)
	{
		HandleListChannels(Args);
		HandleListSpeakers(Args);
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
	Make(TEXT("Kz.Dialogue.Skip"), TEXT("Skip [Channel]"), HandleSkip);
	Make(TEXT("Kz.Dialogue.Pause"), TEXT("Pause [Channel]"), HandlePause);
	Make(TEXT("Kz.Dialogue.Resume"), TEXT("Resume [Channel]"), HandleResume);
	Make(TEXT("Kz.Dialogue.SetSpeed"), TEXT("SetSpeed <factor> [Channel]"), HandleSetSpeed);
	Make(TEXT("Kz.Dialogue.ListChannels"), TEXT("List active channels and their state"), HandleListChannels);
	Make(TEXT("Kz.Dialogue.ListSpeakers"), TEXT("List speaker components in the active world"), HandleListSpeakers);
	Make(TEXT("Kz.Dialogue.Status"), TEXT("Dump full dialogue state"), HandleStatus);
}

FKzDialogueDebugCommands::~FKzDialogueDebugCommands()
{
	// FAutoConsoleCommand RAII handles unregistration on destruction.
	Commands.Reset();
}

#endif // !UE_BUILD_SHIPPING