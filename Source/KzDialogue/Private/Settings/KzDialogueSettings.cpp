// Copyright 2026 kirzo

#include "Settings/KzDialogueSettings.h"
#include "Sound/SoundClass.h"

UE_DISABLE_OPTIMIZATION

namespace Kz::Tags::Dialogue
{
	UE_DEFINE_GAMEPLAY_TAG(MainChannel, "Dialogue.Channel.Main");
	UE_DEFINE_GAMEPLAY_TAG(BarkChannel, "Dialogue.Channel.Bark");
	UE_DEFINE_GAMEPLAY_TAG(SystemChannel, "Dialogue.Channel.System");
	UE_DEFINE_GAMEPLAY_TAG(SpeakerBase, "Dialogue.Speaker");
}

UKzDialogueSettings::UKzDialogueSettings()
{
	// Sensible defaults so a fresh project boots with usable channels.
	// Authors override in Project Settings.
	DefaultChannel = Kz::Tags::Dialogue::MainChannel;

	{
		FKzDialogueChannelDefinition Main;
		Main.Tag = DefaultChannel;
		Main.DisplayName = NSLOCTEXT("KzDialogue", "ChannelMain", "Main");
		Main.DefaultPriority = 100;
		Main.MinPriority = 0;
		Main.MaxPriority = 1000;
		Main.bAllowInterruption = true;
		Channels.Add(Main);
	}
	{
		FKzDialogueChannelDefinition Bark;
		Bark.Tag = Kz::Tags::Dialogue::BarkChannel;
		Bark.DisplayName = NSLOCTEXT("KzDialogue", "ChannelBark", "Bark");
		Bark.DefaultPriority = 10;
		Bark.MinPriority = 0;
		Bark.MaxPriority = 50;
		Bark.bAllowInterruption = true;
		Channels.Add(Bark);
	}
	{
		FKzDialogueChannelDefinition System;
		System.Tag = Kz::Tags::Dialogue::SystemChannel;
		System.DisplayName = NSLOCTEXT("KzDialogue", "ChannelSystem", "System");
		System.DefaultPriority = 500;
		System.MinPriority = 100;
		System.MaxPriority = 1000;
		System.bAllowInterruption = false;
		Channels.Add(System);
	}
}

const FKzDialogueChannelDefinition* UKzDialogueSettings::FindChannel(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid()) { return nullptr; }
	return Channels.FindByPredicate([&Tag](const FKzDialogueChannelDefinition& Def)
		{
			return Def.Tag == Tag;
		});
}

FGameplayTag UKzDialogueSettings::FindChannelForSoundClass(const USoundClass* SoundClass) const
{
	// Walk up the SoundClass hierarchy: an exact entry is a deliberate override and wins, else the
	// nearest mapped ancestor applies, so sub-classes route without hand-adding each one.
	// TSoftObjectPtr keys hash by path, so a pointer-built key matches the configured entry.
	// An entry mapped to an invalid channel counts as unmapped: the walk (and chain) move on.
	int32 Guard = 32; // authored hierarchies are shallow; bail out on a pathological cycle
	for (const USoundClass* Current = SoundClass; Current && Guard-- > 0; Current = Current->ParentClass)
	{
		const FGameplayTag* Found = SoundClassChannels.Find(TSoftObjectPtr<USoundClass>(const_cast<USoundClass*>(Current)));
		if (Found && Found->IsValid())
		{
			return *Found;
		}
	}
	return FGameplayTag();
}

UE_ENABLE_OPTIMIZATION