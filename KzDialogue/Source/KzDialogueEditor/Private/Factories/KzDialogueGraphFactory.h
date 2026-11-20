// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzGraphFactory.h"
#include "KzDialogueGraphFactory.generated.h"

/** New dialogue graphs come with their own lines asset next to them. */
UCLASS()
class UKzDialogueGraphFactory : public UKzGraphFactory
{
	GENERATED_BODY()

public:
	UKzDialogueGraphFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};