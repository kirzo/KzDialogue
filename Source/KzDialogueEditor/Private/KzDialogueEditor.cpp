// Copyright 2026 kirzo

#pragma once

#include "KzDialogueEditor.h"
#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "KzDialogueAsset.h"

#include "Editors/KzArrayAssetEditor.h"

#include "ISequencerModule.h"
#include "Sequencer/KzDialogueTrackEditor.h"

#define LOCTEXT_NAMESPACE "FKzDialogueEditorModule"

void FKzDialogueEditorModule::OnStartupModule()
{
	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(KzAssetCategoryBit, INVTEXT("Dialogue"), FColor::FromHex("#4A6EB6"), { INVTEXT("Dialogues") }, GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Lines), INVTEXT("Line"), MakeShared<FKzDialogueLineRowCustomizer>());

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	DialogueTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FKzDialogueTrackEditor::CreateTrackEditor));
}

void FKzDialogueEditorModule::OnShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("Sequencer"))
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.UnRegisterTrackEditor(DialogueTrackEditorHandle);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKzDialogueEditorModule, KzDialogueEditor);