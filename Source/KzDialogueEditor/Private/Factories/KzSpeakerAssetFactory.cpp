// Copyright 2026 kirzo

#include "Factories/KzSpeakerAssetFactory.h"
#include "KzSpeakerAsset.h"

UKzSpeakerAssetFactory::UKzSpeakerAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UKzSpeakerAsset::StaticClass();
}

UObject* UKzSpeakerAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UKzSpeakerAsset>(InParent, Class, Name, Flags | RF_Transactional);
}