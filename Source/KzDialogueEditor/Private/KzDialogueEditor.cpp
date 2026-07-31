// Copyright 2026 kirzo

#pragma once

#include "KzDialogueEditor.h"
#include "KzDialogueEditorStyle.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "Customizations/KzDialogueAliasRowCustomizer.h"
#include "Customizations/KzDialogueLinePinFactory.h"
#include "Customizations/KzDialogueLineRefCustomization.h"
#include "Customizations/KzDialogueLineListCustomization.h"
#include "Customizations/KzDialogueAliasCustomization.h"
#include "Customizations/KzDialogueTimelineCustomizations.h"
#include "Customizations/KzDialogueLineCustomization.h"
#include "KzDialogueTimeline.h"

#include "Dashboard/SKzDialogueDashboard.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Localization/SKzDialogueCoveragePanel.h"

#include "EdGraphUtilities.h"
#include "ISequencerModule.h"
#include "MessageLogModule.h"
#include "Sequencer/KzDialogueTrackEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

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

	// Extra non-array tab: per-culture localization coverage.
	TArray<FKzCustomEditorTabConfig> DialogueCustomTabs;
	{
		FKzCustomEditorTabConfig& CoverageTab = DialogueCustomTabs.AddDefaulted_GetRef();
		CoverageTab.TabId = TEXT("KzDialogue_L10NCoverage");
		CoverageTab.Label = INVTEXT("Localization");
		CoverageTab.IconStyleName = TEXT("LevelEditor.Tabs.StatsViewer");
		CoverageTab.MakeWidget = [](UObject* Asset) -> TSharedRef<SWidget>
		{
			return SNew(SKzDialogueCoveragePanel, Cast<UKzDialogueAsset>(Asset));
		};
	}

	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(
		KzAssetCategoryBit,
		INVTEXT("Dialogue"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") },
		DialogueTabs,
		DialogueCustomTabs);

	// Speaker assets use the default simple editor: four name fields need no custom toolkit.
	RegisterAssetTypeAction<UKzSpeakerAsset>(
		KzAssetCategoryBit,
		INVTEXT("Dialogue Speaker"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") });

	RegisterPropertyLayout<FKzDialogueAlias, FKzDialogueAliasCustomization>();
	RegisterPropertyLayout<FKzDialogueLineRef, FKzDialogueLineRefCustomization>();
	RegisterPropertyLayout<FKzDialogueLineList, FKzDialogueLineListCustomization>();
	RegisterPropertyLayout<FKzDialogueNotifyTrack, FKzDialogueNotifyTrackCustomization>();
	RegisterPropertyLayout<FKzDialogueNotifyEvent, FKzDialogueNotifyEventCustomization>();
	RegisterPropertyLayout<FKzDialogueLine, FKzDialogueLineCustomization>();

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	DialogueTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FKzDialogueTrackEditor::CreateTrackEditor));

	LinePinFactory = MakeShared<FKzDialogueLinePinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(LinePinFactory);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FKzDialogueTranslationCsv::RegisterMenus));

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.RegisterLogListing(TEXT("KzDialogueL10N"), LOCTEXT("L10NLogLabel", "KzDialogue Localization"));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SKzDialogueDashboard::TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				.Label(LOCTEXT("DashboardTabLabel", "Dialogue Dashboard"))
				[
					SNew(SKzDialogueDashboard)
				];
		}))
		.SetDisplayName(LOCTEXT("DashboardTabName", "Dialogue Dashboard"))
		.SetTooltipText(LOCTEXT("DashboardTabTip", "Every dialogue asset in the project: line and voice counts, speakers, translation batch tools."))
		// Production tool: straight under Tools (the Debug category is for debug-only viewers).
		.SetGroup(FModuleManager::LoadModuleChecked<FWorkspaceMenuStructureModule>("WorkspaceMenuStructure").GetWorkspaceMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FKzDialogueEditorStyle::Get().GetStyleSetName(), "Kz.Dialogue.Icon"));
}

void FKzDialogueEditorModule::OnShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SKzDialogueDashboard::TabId);

	FKzDialogueEditorStyle::Shutdown();

	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwner(TEXT("KzDialogueEditor"));
	}

	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing(TEXT("KzDialogueL10N"));
	}

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