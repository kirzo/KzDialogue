// Copyright 2026 kirzo

#include "Factories/KzWordAssetFactory.h"
#include "KzWordAsset.h"

UKzWordAssetFactory::UKzWordAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UKzWordAsset::StaticClass();
}

UObject* UKzWordAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UKzWordAsset>(InParent, Class, Name, Flags | RF_Transactional);
}