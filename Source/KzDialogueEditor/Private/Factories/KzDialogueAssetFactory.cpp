// Copyright 2026 kirzo

#include "Factories/KzDialogueAssetFactory.h"
#include "KzDialogueAsset.h"

UKzDialogueAssetFactory::UKzDialogueAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UKzDialogueAsset::StaticClass();
}

UObject* UKzDialogueAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UKzDialogueAsset>(InParent, Class, Name, Flags | RF_Transactional);
}