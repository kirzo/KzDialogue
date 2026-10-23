// Copyright 2026 kirzo

#include "KzDialogueEditor.h"
#include "KzDialogueEditorStyle.h"
#include "KzDialogueAudition.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"
#include "KzSpeakerAsset.h"
#include "KzWordAsset.h"

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "Customizations/KzDialogueAliasRowCustomizer.h"
#include "Customizations/KzDialogueLinePinFactory.h"
#include "Customizations/KzDialogueLineRefCustomization.h"
#include "Customizations/KzDialogueLineListCustomization.h"
#include "Customizations/KzDialogueAliasCustomization.h"
#include "Customizations/KzDialogueTimelineCustomizations.h"
#include "Customizations/KzDialogueLineCustomization.h"
#include "Customizations/KzTokenTextCustomization.h"
#include "KzDialogueTimeline.h"
#include "KzTokenText.h"

#include "Dashboard/SKzDialogueDashboard.h"
#include "Editors/KzArrayAssetEditor.h"
#include "Localization/KzL10N.h"
#include "Localization/SKzDialogueCoveragePanel.h"
#include "KzDialogueK2Nodes.h"

#include "EdGraphUtilities.h"
#include "Editor.h"
#include "ISequencerModule.h"
#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"
#include "Widgets/Input/SButton.h"
#include "Sequencer/KzDialogueTrackEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
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

	TArray<FKzCustomEditorTabConfig> DialogueCustomTabs;
	{
		FKzCustomEditorTabConfig& CoverageTab = DialogueCustomTabs.AddDefaulted_GetRef();
		CoverageTab.TabId = TEXT("KzDialogue_L10NCoverage");
		CoverageTab.Label = INVTEXT("Localization");
		CoverageTab.IconStyleName = TEXT("LevelEditor.Tabs.StatsViewer");
		CoverageTab.MakeWidget = [](UObject* Asset) -> TSharedRef<SWidget>
		{
			return SNew(SKzDialogueCoveragePanel, TArray<UKzDialogueAsset*>{ Cast<UKzDialogueAsset>(Asset) })
				.ToolbarExtension()
				[
					SNew(SButton)
						.Text(NSLOCTEXT("KzDialogueEditor", "OpenDashboard", "Open Dashboard"))
						.ToolTipText(NSLOCTEXT("KzDialogueEditor", "OpenDashboardTip", "Open the project-wide Dialogue Dashboard."))
						.OnClicked_Lambda([]()
						{
							FGlobalTabmanager::Get()->TryInvokeTab(SKzDialogueDashboard::TabId);
							return FReply::Handled();
						})
				];
		};
	}

	RegisterAssetTypeAction<UKzDialogueAsset, FKzArrayAssetEditor>(
		KzAssetCategoryBit,
		INVTEXT("Dialogue"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") },
		DialogueTabs,
		DialogueCustomTabs);

	RegisterAssetTypeAction<UKzSpeakerAsset>(
		KzAssetCategoryBit,
		INVTEXT("Dialogue Speaker"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") });

	RegisterAssetTypeAction<UKzWordAsset>(
		KzAssetCategoryBit,
		INVTEXT("Word"),
		FKzDialogueEditorStyle::BrandColor.ToFColor(true),
		{ INVTEXT("Dialogues") });

	RegisterPropertyLayout<FKzDialogueAlias, FKzDialogueAliasCustomization>();
	RegisterPropertyLayout<FKzDialogueLineRef, FKzDialogueLineRefCustomization>();
	RegisterPropertyLayout<FKzDialogueLineList, FKzDialogueLineListCustomization>();
	RegisterPropertyLayout<FKzDialogueNotifyTrack, FKzDialogueNotifyTrackCustomization>();
	RegisterPropertyLayout<FKzDialogueNotifyEvent, FKzDialogueNotifyEventCustomization>();
	RegisterPropertyLayout<FKzDialogueLine, FKzDialogueLineCustomization>();
	RegisterPropertyLayout<FKzTokenText, FKzTokenTextCustomization>();

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	DialogueTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FKzDialogueTrackEditor::CreateTrackEditor));

	LinePinFactory = MakeShared<FKzDialogueLinePinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(LinePinFactory);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&KzDialogueL10N::RegisterMenus));

	// Token renames refactor the lines and archives that reference the old token.
	NamedTokenRenamedHandle = UKzNamedAsset::OnTokenRenamed.AddStatic(&KzDialogueL10N::RenameNamedTokenReferences);

	// The audition is a world-less editor preview: nothing else stops it when the game starts.
	BeginPIEHandle = FEditorDelegates::BeginPIE.AddLambda([](bool) { KzDialogueAudition::Stop(); });

	// The line nodes live in the uncooked module, which cannot reach the asset editor.
	KzDialogueK2Nodes::OpenAssetAtEntry = &FKzDialogueEditorModule::OpenAssetAtEntry;

	FMessageLogInitializationOptions LogOptions;
	LogOptions.bShowPages = true;
	LogOptions.bAllowClear = true;
	FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog").RegisterLogListing(KzDialogueL10N::MessageLogName, LOCTEXT("L10NLogLabel", "Dialogue Localization"), LogOptions);

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
		.SetTooltipText(LOCTEXT("DashboardTabTip", "Localization status of every dialogue asset in the project: per-culture progress, per-line states, CSV round-trip and the Gather/Compile pipeline."))
		.SetGroup(FModuleManager::LoadModuleChecked<FWorkspaceMenuStructureModule>("WorkspaceMenuStructure").GetWorkspaceMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FKzDialogueEditorStyle::Get().GetStyleSetName(), "Kz.Dialogue.Icon"));
}

void FKzDialogueEditorModule::OnShutdownModule()
{
	UKzNamedAsset::OnTokenRenamed.Remove(NamedTokenRenamedHandle);
	FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
	KzDialogueK2Nodes::OpenAssetAtEntry = nullptr;

	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing(KzDialogueL10N::MessageLogName);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SKzDialogueDashboard::TabId);

	FKzDialogueEditorStyle::Shutdown();

	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwner(TEXT("KzDialogueEditor"));
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

void FKzDialogueEditorModule::OpenAssetAtEntry(UKzDialogueAsset* Asset, const FGuid& EntryId)
{
	if (!Asset || !GEditor) { return; }

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Subsystem->OpenEditorForAsset(Asset);

	// The entry selects in its own tab (Lines, or Aliases), as the validation panel's clicks do.
	IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false);
	if (EntryId.IsValid() && Instance && Instance->GetEditorName() == TEXT("KzArrayAssetEditor"))
	{
		static_cast<FKzArrayAssetEditor*>(Instance)->SelectElementById(EntryId);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKzDialogueEditorModule, KzDialogueEditor);