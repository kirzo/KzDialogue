// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
struct FAssetData;

/**
 * Project-wide dialogue localization dashboard: the shared localization panel
 * (SKzDialogueCoveragePanel) fed with every UKzDialogueAsset in the project, plus the
 * Gather Text / Compile Text pipeline buttons in its toolbar. Rebuilds when dialogue
 * assets are added, removed or renamed; the panel itself follows line edits.
 * Opened from Tools > Dialogue Dashboard.
 */
class SKzDialogueDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueDashboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SKzDialogueDashboard() override;

	/** Nomad tab id the dashboard is registered under; TryInvokeTab with it to open/focus the dashboard. */
	static const FName TabId;

private:
	TSharedPtr<SBox> PanelHost;

	/** Loads every dialogue asset in the project and rebuilds the shared localization panel around them. */
	void RebuildPanel();

	/** Runs the engine's Gather Text / Compile Text commandlet for the plugin's localization target, without opening the Localization Dashboard. */
	FReply OnGatherClicked();

	/** Compile menu: every culture at once, or one culture alone (only its .locres changes, which keeps source control clean after a single-language edit). */
	TSharedRef<SWidget> BuildCompileMenu();

	/** Compile Text for the whole target, or for one culture when given. */
	void RunCompile(FString Culture);

	void OnAssetRegistryChanged(const FAssetData& Data);
	void OnAssetRenamed(const FAssetData& Data, const FString& OldPath);
	void OnRegistryFilesLoaded();
};