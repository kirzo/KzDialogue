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

	/** Server time the line taken last started at there, and whether it reached this machine already well under way. */
	double TakenLineEnterTime = 0.0;
	bool bTakenLineLate = false;

	/** The line to show next; it replaces one not shown yet, which the server already left behind. */
	void SetNextLine(FKzDialogueLine&& Line, double EnterTime, bool bLate)
	{
		NextLine = MoveTemp(Line);
		NextLineEnterTime = EnterTime;
		bNextLineLate = bLate;
		bHasNextLine = true;
	}

	virtual bool HasNext_Implementation() const override { return bHasNextLine && !bEnded; }
	virtual FKzDialogueLine Advance_Implementation() override
	{
		bHasNextLine = false;
		TakenLineEnterTime = NextLineEnterTime;
		bTakenLineLate = bNextLineLate;
		return MoveTemp(NextLine);
	}
	virtual FKzDialogueLine PeekNext_Implementation() const override { return bHasNextLine ? NextLine : FKzDialogueLine{}; }

	/** Until the server's next line, or its end. */
	virtual bool IsWaiting_Implementation() const override { return !bEnded; }

	virtual FString GetHistoryLabel() const override { return TEXT("copy of the server's player"); }

private:
	UPROPERTY(Transient)
	FKzDialogueLine NextLine;

	double NextLineEnterTime = 0.0;
	bool bNextLineLate = false;
	bool bHasNextLine = false;
};