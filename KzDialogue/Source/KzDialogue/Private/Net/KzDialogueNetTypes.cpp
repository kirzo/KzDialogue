// Copyright 2026 kirzo

#include "Net/KzDialogueNetTypes.h"
#include "KzDialogueAsset.h"
#include "KzDialogueSubsystem.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// ---------------------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------------------

FKzDialogueNetArgument::FKzDialogueNetArgument(const FString& InName, const FFormatArgumentValue& Value)
	: Name(InName)
	, Type(static_cast<uint8>(Value.GetType()))
{
	switch (Value.GetType())
	{
	case EFormatArgumentType::Int: Integer = Value.GetIntValue(); break;
	case EFormatArgumentType::UInt: Integer = static_cast<int64>(Value.GetUIntValue()); break;
	case EFormatArgumentType::Float: Number = Value.GetFloatValue(); break;
	case EFormatArgumentType::Double: Number = Value.GetDoubleValue(); break;
	case EFormatArgumentType::Text: Text = Value.GetTextValue(); break;
	case EFormatArgumentType::Gender: Integer = static_cast<int64>(Value.GetGenderValue()); break;
	default: break;
	}
}

FFormatArgumentValue FKzDialogueNetArgument::ToValue() const
{
	switch (static_cast<EFormatArgumentType::Type>(Type))
	{
	case EFormatArgumentType::Int: return FFormatArgumentValue(Integer);
	case EFormatArgumentType::UInt: return FFormatArgumentValue(static_cast<uint64>(Integer));
	case EFormatArgumentType::Float: return FFormatArgumentValue(static_cast<float>(Number));
	case EFormatArgumentType::Double: return FFormatArgumentValue(Number);
	case EFormatArgumentType::Gender: return FFormatArgumentValue(static_cast<ETextGender>(Integer));
	default: return FFormatArgumentValue(Text);
	}
}

FKzDialogueNetLine::FKzDialogueNetLine(const FKzDialogueLine& InLine)
{
	if (const UKzDialogueAsset* Source = InLine.SourceAsset.Get())
	{
		Asset = TSoftObjectPtr<UKzDialogueAsset>(FSoftObjectPath(Source));
		LineId = InLine.LineId;
	}
	else
	{
		Line.Add(InLine);
	}

	for (const TPair<FString, FFormatArgumentValue>& Argument : InLine.FormatArguments)
	{
		Arguments.Emplace(Argument.Key, Argument.Value);
	}
}

bool FKzDialogueNetLine::Resolve(FKzDialogueLine& OutLine) const
{
	if (!Line.IsEmpty())
	{
		OutLine = Line[0];
	}
	else
	{
		const UKzDialogueAsset* Source = Asset.LoadSynchronous();
		if (!Source || !Source->TryGetLineById(LineId, OutLine))
		{
			return false;
		}
	}

	for (const FKzDialogueNetArgument& Argument : Arguments)
	{
		OutLine.FormatArguments.Add(Argument.Name, Argument.ToValue());
	}
	return true;
}

// ---------------------------------------------------------------------------------------
// Entries
// ---------------------------------------------------------------------------------------

static UKzDialogueSubsystem* GetHostSubsystem(const UObject* Host)
{
	const UWorld* World = Host ? Host->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
}

static UKzDialogueSubsystem* GetEntriesSubsystem(const FKzDialogueNetEntries& InArray)
{
	return GetHostSubsystem(InArray.Host);
}

// The host's actor sends what changed with its next net update, asked for now.
static void ForceHostUpdate(UObject* Host)
{
	AActor* Actor = Cast<AActor>(Host);
	if (!Actor)
	{
		const UActorComponent* Component = Cast<UActorComponent>(Host);
		Actor = Component ? Component->GetOwner() : nullptr;
	}
	if (Actor)
	{
		Actor->ForceNetUpdate();
	}
}

void FKzDialogueNetEntry::PostReplicatedAdd(const FKzDialogueNetEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetEntriesSubsystem(InArray)) { Subsystem->ReceiveNetEntry(*this); }
}

void FKzDialogueNetEntry::PostReplicatedChange(const FKzDialogueNetEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetEntriesSubsystem(InArray)) { Subsystem->ReceiveNetEntry(*this); }
}

void FKzDialogueNetEntry::PreReplicatedRemove(const FKzDialogueNetEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetEntriesSubsystem(InArray)) { Subsystem->ReleaseNetEntry(*this); }
}

FKzDialogueNetEntry& FKzDialogueNetEntries::FindOrAdd(FGameplayTag Channel, AActor* Owner)
{
	if (FKzDialogueNetEntry* Existing = Entries.FindByPredicate([Channel, Owner](const FKzDialogueNetEntry& Entry) { return Entry.Channel == Channel && Entry.Owner == Owner; }))
	{
		return *Existing;
	}

	FKzDialogueNetEntry& Added = Entries.AddDefaulted_GetRef();
	Added.Channel = Channel;
	Added.Owner = Owner;
	Added.bHasOwner = Owner != nullptr;
	return Added;
}

void FKzDialogueNetEntries::Remove(FGameplayTag Channel, const AActor* Owner)
{
	if (Entries.RemoveAll([Channel, Owner](const FKzDialogueNetEntry& Entry) { return Entry.Channel == Channel && Entry.Owner == Owner; }) > 0)
	{
		MarkArrayDirty();
		ForceHostUpdate(Host);
	}
}

void FKzDialogueNetEntries::Commit(FKzDialogueNetEntry& Entry)
{
	MarkItemDirty(Entry);
	ForceHostUpdate(Host);
}

// ---------------------------------------------------------------------------------------
// Graph entries
// ---------------------------------------------------------------------------------------

void FKzDialogueNetGraphEntry::PostReplicatedAdd(const FKzDialogueNetGraphEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetHostSubsystem(InArray.Host)) { Subsystem->ReceiveNetGraphEntry(*this); }
}

void FKzDialogueNetGraphEntry::PostReplicatedChange(const FKzDialogueNetGraphEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetHostSubsystem(InArray.Host)) { Subsystem->ReceiveNetGraphEntry(*this); }
}

void FKzDialogueNetGraphEntry::PreReplicatedRemove(const FKzDialogueNetGraphEntries& InArray)
{
	if (UKzDialogueSubsystem* Subsystem = GetHostSubsystem(InArray.Host)) { Subsystem->ReleaseNetGraphEntry(*this); }
}

FKzDialogueNetGraphEntry& FKzDialogueNetGraphEntries::FindOrAdd(int32 PlayId)
{
	if (FKzDialogueNetGraphEntry* Existing = Entries.FindByPredicate([PlayId](const FKzDialogueNetGraphEntry& Entry) { return Entry.PlayId == PlayId; }))
	{
		return *Existing;
	}

	FKzDialogueNetGraphEntry& Added = Entries.AddDefaulted_GetRef();
	Added.PlayId = PlayId;
	return Added;
}

void FKzDialogueNetGraphEntries::Remove(int32 PlayId)
{
	if (Entries.RemoveAll([PlayId](const FKzDialogueNetGraphEntry& Entry) { return Entry.PlayId == PlayId; }) > 0)
	{
		MarkArrayDirty();
		ForceHostUpdate(Host);
	}
}

void FKzDialogueNetGraphEntries::Commit(FKzDialogueNetGraphEntry& Entry)
{
	MarkItemDirty(Entry);
	ForceHostUpdate(Host);
}