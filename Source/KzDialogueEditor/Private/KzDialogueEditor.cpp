// Copyright 2026 kirzo

#pragma once

#include "KzDialogueEditor.h"
#include "KzDialogueEditorStyle.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "Customizations/KzDialogueAliasRowCustomizer.h"
#include "Customizations/KzDialogueLinePinFactory.h"
#include "Customizations/KzDialogueLineRefCustomization.h"
#include "Customizations/KzDialogueAliasCustomization.h"

#include "Editors/KzArrayAssetEditor.h"

#include "EdGraphUtilities.h"
#include "ISequencerModule.h"
#include "Sequencer/KzDialogueTrackEditor.h"

#define LOCTEXT_NAMESPACE "FKzDialogueEditorModule"

void FKzDialogueEditorModule::OnStartupModule()
{
	FKzDialogueEditorStyle::Initialize();

	TArray<FKzArrayEditorTabConfig> DialogueTabs;
	DialogueTabs.Add(FKzArrayEditorTabConfig(
		GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Lines),
		INVTEXT("Line"),
		MakeShared<FKzDialogueLineRowCustomizer>()));

	DialogueTabs.Add(FKzArrayEditorTabConfig(
		GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Aliases),
		INVTEXT("Alias"),
		MakeShared<FKzDialogueAliasRowCustomizer>()));

	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(
		KzAssetCategoryBit,
		INVTEXT("Dialogue"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") },
		DialogueTabs);

	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(KzAssetCategoryBit, INVTEXT("Dialogue"), FKzDialogueEditorStyle::BrandColor.ToFColor(true), {INVTEXT("Dialogues")}, DialogueTabs);
	RegisterPropertyLayout<FKzDialogueAlias, FKzDialogueAliasCustomization>();
	RegisterPropertyLayout<FKzDialogueLineRef, FKzDialogueLineRefCustomization>();

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	DialogueTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FKzDialogueTrackEditor::CreateTrackEditor));

	LinePinFactory = MakeShared<FKzDialogueLinePinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(LinePinFactory);
}

void FKzDialogueEditorModule::OnShutdownModule()
{
	FKzDialogueEditorStyle::Shutdown();

	if (FModuleManager::Get().IsModuleLoaded("Sequencer"))
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.UnRegisterTrackEditor(DialogueTrackEditorHandle);
	}

	if (LinePinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(LinePinFactory);
		LinePinFactory.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKzDialogueEditorModule, KzDialogueEditor);