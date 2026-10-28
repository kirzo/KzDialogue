// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzDialogueProvider.h"
#include "KzDialogueNetProvider.generated.h"

/** What a copy of a server player plays: the server's latest line, waiting between them, and nothing more once the server's dialogue ends. */
UCLASS(NotBlueprintable)
class UKzDialogueNetProvider : public UKzDialogueProvider
{
	GENERATED_BODY()

public:
	/** The server's dialogue has no more lines. */
	bool bEnded = false;

	/** The line to show next; it replaces one not shown yet, which the server already left behind. */
	void SetNextLine(FKzDialogueLine&& Line)
	{
		NextLine = MoveTemp(Line);
		bHasNextLine = true;
	}

	virtual bool HasNext_Implementation() const override { return bHasNextLine && !bEnded; }
	virtual FKzDialogueLine Advance_Implementation() override
	{
		bHasNextLine = false;
		return MoveTemp(NextLine);
	}
	virtual FKzDialogueLine PeekNext_Implementation() const override { return bHasNextLine ? NextLine : FKzDialogueLine{}; }

	/** Until the server's next line, or its end. */
	virtual bool IsWaiting_Implementation() const override { return !bEnded; }

	virtual FString GetHistoryLabel() const override { return TEXT("copy of the server's player"); }

private:
	UPROPERTY(Transient)
	FKzDialogueLine NextLine;

	bool bHasNextLine = false;
};