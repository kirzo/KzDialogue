// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "KzDialogueTypes.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "KzDialogueNetTypes.generated.h"

class AActor;
class UKzDialogueAsset;
class UKzDialogueGraph;
struct FKzDialogueNetEntries;
struct FKzDialogueNetGraphEntries;

/** Where a server player's dialogue is, for its copies on the clients to follow. */
UENUM()
enum class EKzDialogueNetPhase : uint8
{
	/** No dialogue: none started yet, or the last one ended with FinishReason. */
	Idle,
	/** Showing its lines. */
	Playing,
	/** Leaving the line shown; the next one, or the end, comes after. */
	LineExiting,
	/** Open with no line shown, until the next one comes. */
	Waiting,
	/** Past its last line, playing its exit. */
	Exiting
};

/** A call site format argument of a line, as it travels. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetArgument
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** EFormatArgumentType, which tells which value below it uses. */
	UPROPERTY()
	uint8 Type = 0;

	UPROPERTY()
	int64 Integer = 0;

	UPROPERTY()
	double Number = 0.0;

	UPROPERTY()
	FText Text;

	FKzDialogueNetArgument() = default;
	FKzDialogueNetArgument(const FString& InName, const FFormatArgumentValue& Value);

	FFormatArgumentValue ToValue() const;
};

/** A line as it travels to the clients: its asset and id, which each client resolves in its own language, or the whole line when it was built by hand. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetLine
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UKzDialogueAsset> Asset;

	UPROPERTY()
	FGuid LineId;

	/** Empty for a line of an asset; the whole line, alone, for one built by hand. */
	UPROPERTY()
	TArray<FKzDialogueLine> Line;

	UPROPERTY()
	TArray<FKzDialogueNetArgument> Arguments;

	FKzDialogueNetLine() = default;
	explicit FKzDialogueNetLine(const FKzDialogueLine& InLine);

	/** The line to show on this machine. False when its asset no longer has it. */
	bool Resolve(FKzDialogueLine& OutLine) const;
};

/** One server player, for its copy on each client that shows it to follow. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Channel;

	/** The player's owner; also null on a client the server's owner is not relevant to. */
	UPROPERTY()
	TObjectPtr<AActor> Owner = nullptr;

	/** The server's player has an owner: a null Owner means not relevant here, not a global player. */
	UPROPERTY()
	bool bHasOwner = false;

	UPROPERTY()
	EKzDialogueNetPhase Phase = EKzDialogueNetPhase::Idle;

	/** How the last dialogue ended, while Idle. */
	UPROPERTY()
	EKzDialogueFinishReason FinishReason = EKzDialogueFinishReason::Completed;

	UPROPERTY()
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

	UPROPERTY()
	bool bPaused = false;

	/** Counts the dialogues started, so a copy tells a new one from the one it shows. */
	UPROPERTY()
	int32 DialogueSerial = 0;

	/** Counts the lines of the dialogue, so a copy tells a new line from the one it shows. */
	UPROPERTY()
	int32 LineSerial = 0;

	UPROPERTY()
	FKzDialogueNetLine Line;

	/** Server time the line was taken at, so a copy that gets it late joins it where it goes. */
	UPROPERTY()
	double LineEnterTime = 0.0;

	void PostReplicatedAdd(const FKzDialogueNetEntries& InArray);
	void PostReplicatedChange(const FKzDialogueNetEntries& InArray);
	void PreReplicatedRemove(const FKzDialogueNetEntries& InArray);
};

/** The server players an actor carries to the clients: the net actor the shared ones, a player's net component that player's own. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetEntries : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FKzDialogueNetEntry> Entries;

	/** The actor or component carrying it, whose world the entries arrive in. */
	UPROPERTY(NotReplicated)
	TObjectPtr<UObject> Host = nullptr;

	FKzDialogueNetEntry& FindOrAdd(FGameplayTag Channel, AActor* Owner);
	void Remove(FGameplayTag Channel, const AActor* Owner);

	/** Sends the entry with the host's next net update, asked for now. */
	void Commit(FKzDialogueNetEntry& Entry);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FKzDialogueNetEntry, FKzDialogueNetEntries>(Entries, DeltaParams, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FKzDialogueNetEntries> : public TStructOpsTypeTraitsBase2<FKzDialogueNetEntries>
{
	enum { WithNetDeltaSerializer = true };
};

/** An option of a Choice as it travels; each client resolves its line in its own language. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetOption
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Index = INDEX_NONE;

	UPROPERTY()
	bool bAvailable = true;

	UPROPERTY()
	bool bDefault = false;

	UPROPERTY()
	FKzDialogueNetLine Line;
};

/** One server play of a dialogue graph, for its copy on each client that shows it. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetGraphEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Names the play across the network. */
	UPROPERTY()
	int32 PlayId = 0;

	/** The channel of the player its lines go to. */
	UPROPERTY()
	FGameplayTag Channel;

	/** The owner of the player its lines go to; also null on a client the server's owner is not relevant to. */
	UPROPERTY()
	TObjectPtr<AActor> Owner = nullptr;

	UPROPERTY()
	bool bHasOwner = false;

	/** The graph the play is in now. */
	UPROPERTY()
	TObjectPtr<UKzDialogueGraph> Graph = nullptr;

	UPROPERTY()
	FKzDialogueGraphContext Context;

	/** Counts the offers of options, so a copy tells a new one from the one it shows. */
	UPROPERTY()
	int32 OfferSerial = 0;

	/** The options offered now; empty when no Choice waits. */
	UPROPERTY()
	TArray<FKzDialogueNetOption> Options;

	/** Server time the options' time limit runs out at; 0 without one. */
	UPROPERTY()
	double ChoiceEndTime = 0.0;

	void PostReplicatedAdd(const FKzDialogueNetGraphEntries& InArray);
	void PostReplicatedChange(const FKzDialogueNetGraphEntries& InArray);
	void PreReplicatedRemove(const FKzDialogueNetGraphEntries& InArray);
};

/** The server's dialogue graph plays an actor carries to the clients, next to their players' entries. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueNetGraphEntries : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FKzDialogueNetGraphEntry> Entries;

	/** The actor or component carrying it, whose world the entries arrive in. */
	UPROPERTY(NotReplicated)
	TObjectPtr<UObject> Host = nullptr;

	FKzDialogueNetGraphEntry& FindOrAdd(int32 PlayId);
	void Remove(int32 PlayId);

	/** Sends the entry with the host's next net update, asked for now. */
	void Commit(FKzDialogueNetGraphEntry& Entry);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FKzDialogueNetGraphEntry, FKzDialogueNetGraphEntries>(Entries, DeltaParams, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FKzDialogueNetGraphEntries> : public TStructOpsTypeTraitsBase2<FKzDialogueNetGraphEntries>
{
	enum { WithNetDeltaSerializer = true };
};