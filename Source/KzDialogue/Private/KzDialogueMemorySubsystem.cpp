// Copyright 2026 kirzo

#include "KzDialogueMemorySubsystem.h"

#include "KzDialogueAsset.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UKzDialogueMemorySubsystem* UKzDialogueMemorySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKzDialogueMemorySubsystem>() : nullptr;
}

bool UKzDialogueMemorySubsystem::HasSeenLine(const FKzDialogueLineRef& Ref) const
{
	return GetLineSeenCount(Ref) > 0;
}

int32 UKzDialogueMemorySubsystem::GetLineSeenCount(const FKzDialogueLineRef& Ref) const
{
	if (const int32* Count = Memory.SeenLines.Find(Ref.LineId)) { return *Count; }

	// Not a seen line id: it may be an alias, which counts its lines.
	const UKzDialogueAsset* Asset = Ref.Asset.LoadSynchronous();
	const FKzDialogueAlias* Alias = Asset ? Asset->FindAlias(Ref.LineId) : nullptr;
	if (!Alias) { return 0; }

	int32 Total = 0;
	for (const FGuid& LineId : Alias->LineIds)
	{
		if (const int32* Count = Memory.SeenLines.Find(LineId)) { Total += *Count; }
	}
	return Total;
}

void UKzDialogueMemorySubsystem::ForgetLine(const FKzDialogueLineRef& Ref)
{
	if (Memory.SeenLines.Remove(Ref.LineId) > 0) { return; }

	const UKzDialogueAsset* Asset = Ref.Asset.LoadSynchronous();
	if (const FKzDialogueAlias* Alias = Asset ? Asset->FindAlias(Ref.LineId) : nullptr)
	{
		for (const FGuid& LineId : Alias->LineIds)
		{
			Memory.SeenLines.Remove(LineId);
		}
		Memory.Aliases.Remove(Alias->AliasId);
	}
}

void UKzDialogueMemorySubsystem::RecordLinePresented(const FKzDialogueLine& Line)
{
	if (Line.SourceAsset.IsValid() && Line.LineId.IsValid())
	{
		++Memory.SeenLines.FindOrAdd(Line.LineId);
	}
}