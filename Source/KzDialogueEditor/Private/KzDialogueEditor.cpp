// Copyright 2026 kirzo

#pragma once

#include "KzDialogueEditor.h"
#include "KzDialogueEditorStyle.h"
#include "KzDialogueAsset.h"

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "Customizations/KzDialogueLinePinFactory.h"

#include "Editors/KzArrayAssetEditor.h"

#include "EdGraphUtilities.h"
#include "ISequencerModule.h"
#include "Sequencer/KzDialogueTrackEditor.h"

#define LOCTEXT_NAMESPACE "FKzDialogueEditorModule"

void FKzDialogueEditorModule::OnStartupModule()
{
	FKzDialogueEditorStyle::Initialize();

	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(KzAssetCategoryBit, INVTEXT("Dialogue"), FKzDialogueEditorStyle::BrandColor.ToFColor(true), {INVTEXT("Dialogues")}, GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Lines), INVTEXT("Line"), MakeShared<FKzDialogueLineRowCustomizer>());

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