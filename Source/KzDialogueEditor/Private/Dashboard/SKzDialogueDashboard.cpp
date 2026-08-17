// Copyright 2026 kirzo

#include "Dashboard/SKzDialogueDashboard.h"

#include "KzDialogueAsset.h"
#include "Localization/SKzDialogueCoveragePanel.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LocalizationCommandletTasks.h"
#include "LocalizationModule.h"
#include "LocalizationTargetTypes.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueDashboard"

const FName SKzDialogueDashboard::TabId(TEXT("KzDialogueDashboard"));

namespace
{
	void ShowDashboardNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	ULocalizationTarget* FindDialogueLocTarget()
	{
		return ILocalizationModule::Get().GetLocalizationTargetByName(GetDefault<UKzDialogueSettings>()->LocalizationTargetName, /*bIsEngineTarget=*/false);
	}
}

void SKzDialogueDashboard::Construct(const FArguments& /*InArgs*/)
{
	ChildSlot
	[
		SAssignNew(PanelHost, SBox)
	];

	// Keep in sync with the registry (adds, deletes, renames). Saves need no rebuild: the
	// panel follows line edits by itself.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.OnFilesLoaded().AddSP(this, &SKzDialogueDashboard::OnRegistryFilesLoaded);
	Registry.OnAssetAdded().AddSP(this, &SKzDialogueDashboard::OnAssetRegistryChanged);
	Registry.OnAssetRemoved().AddSP(this, &SKzDialogueDashboard::OnAssetRegistryChanged);
	Registry.OnAssetRenamed().AddSP(this, &SKzDialogueDashboard::OnAssetRenamed);

	RebuildPanel();
}

SKzDialogueDashboard::~SKzDialogueDashboard()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.OnFilesLoaded().RemoveAll(this);
		Registry.OnAssetAdded().RemoveAll(this);
		Registry.OnAssetRemoved().RemoveAll(this);
		Registry.OnAssetRenamed().RemoveAll(this);
	}
}

void SKzDialogueDashboard::RebuildPanel()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Found;
	Registry.GetAssetsByClass(UKzDialogueAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);

	// The project-wide view is line-level, so every dialogue asset loads here.
	TArray<UKzDialogueAsset*> Loaded;
	Loaded.Reserve(Found.Num());
	{
		FScopedSlowTask SlowTask(static_cast<float>(Found.Num()), LOCTEXT("LoadingAssets", "Loading dialogue assets..."));
		SlowTask.MakeDialog();
		for (const FAssetData& Data : Found)
		{
			SlowTask.EnterProgressFrame(1.f);
			if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset()))
			{
				Loaded.Add(Asset);
			}
		}
	}
	Loaded.Sort([](const UKzDialogueAsset& A, const UKzDialogueAsset& B) { return A.GetName() < B.GetName(); });

	PanelHost->SetContent(
		SNew(SKzDialogueCoveragePanel, Loaded)
		.bIncludeProjectTexts(true)
		.ToolbarExtension()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("GatherText", "Gather"))
					.ToolTipText(LOCTEXT("GatherTextTip", "Run Gather Text on the localization target, same as the Localization Dashboard button. Refreshes manifest and archives from saved assets."))
					.OnClicked(this, &SKzDialogueDashboard::OnGatherClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
					.Text(LOCTEXT("CompileText", "Compile"))
					.ToolTipText(LOCTEXT("CompileTextTip", "Run Compile Text on the localization target: writes the .locres files the game reads at runtime."))
					.OnClicked(this, &SKzDialogueDashboard::OnCompileClicked)
			]
		]);
}

FReply SKzDialogueDashboard::OnGatherClicked()
{
	ULocalizationTarget* Target = FindDialogueLocTarget();
	if (!Target)
	{
		ShowDashboardNotification(LOCTEXT("NoLocTarget", "Localization target not found. Create it once in the Localization Dashboard (Tools menu)."), false);
		return FReply::Handled();
	}

	// Same flow as the engine Localization Dashboard button: gather reads SAVED packages,
	// so dirty ones get the save prompt first, with a bail-out warning when declined.
	bool bPackagesNeededSaving = false;
	const bool bPackagesSaved = FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave=*/true, /*bSaveMapPackages=*/true, /*bSaveContentPackages=*/true, /*bFastSave=*/false, /*bNotifyNoPackagesSaved=*/false, /*bCanBeDeclined=*/true, &bPackagesNeededSaving);
	if (bPackagesNeededSaving && !bPackagesSaved)
	{
		if (FMessageDialog::Open(EAppMsgType::OkCancel, LOCTEXT("UnsavedBeforeGatherMsg", "There are unsaved changes. These changes may not be gathered from correctly."), LOCTEXT("UnsavedBeforeGatherTitle", "Unsaved Changes Before Gather")) == EAppReturnType::Cancel)
		{
			return FReply::Handled();
		}
	}

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!Window.IsValid()) { Window = FSlateApplication::Get().GetActiveTopLevelWindow(); }
	if (!Window.IsValid()) { return FReply::Handled(); }

	if (LocalizationCommandletTasks::GatherTextForTarget(Window.ToSharedRef(), Target))
	{
		// Keep the engine dashboard's word counts / conflict status in sync, like its own button does.
		Target->UpdateWordCountsFromCSV();
		Target->UpdateStatusFromConflictReport();

		// The manifest/archives changed: a fresh panel re-reads them.
		RebuildPanel();
	}
	return FReply::Handled();
}

FReply SKzDialogueDashboard::OnCompileClicked()
{
	ULocalizationTarget* Target = FindDialogueLocTarget();
	if (!Target)
	{
		ShowDashboardNotification(LOCTEXT("NoLocTarget", "Localization target not found. Create it once in the Localization Dashboard (Tools menu)."), false);
		return FReply::Handled();
	}

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!Window.IsValid()) { Window = FSlateApplication::Get().GetActiveTopLevelWindow(); }
	if (!Window.IsValid()) { return FReply::Handled(); }

	// Compile only writes .locres; the archives the panel reads stay valid.
	LocalizationCommandletTasks::CompileTextForTarget(Window.ToSharedRef(), Target);
	return FReply::Handled();
}

void SKzDialogueDashboard::OnAssetRegistryChanged(const FAssetData& Data)
{
	if (Data.AssetClassPath != UKzDialogueAsset::StaticClass()->GetClassPathName()) { return; }

	// The startup scan storms per-asset events; OnFilesLoaded catches up once at the end.
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (Registry.IsLoadingAssets()) { return; }

	RebuildPanel();
}

void SKzDialogueDashboard::OnAssetRenamed(const FAssetData& Data, const FString& /*OldPath*/)
{
	OnAssetRegistryChanged(Data);
}

void SKzDialogueDashboard::OnRegistryFilesLoaded()
{
	RebuildPanel();
}

#undef LOCTEXT_NAMESPACE