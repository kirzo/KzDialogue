// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UKzDialogueAsset;
class SVerticalBox;
struct FKzCultureCoverage;

/**
 * Read-only per-culture translation coverage table, shown as an extra tab in the
 * dialogue asset editor. Pure information: translated / missing / stale texts and
 * localized audio counts per culture, refreshed on demand.
 */
class SKzDialogueCoveragePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueCoveragePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UKzDialogueAsset* InAsset);

	/** Renders the per-culture coverage table (header row + one row per culture) into Rows. Shared with the dialogue dashboard. */
	static void FillCoverageRows(SVerticalBox& Rows, const TArray<FKzCultureCoverage>& Cultures);

private:
	TWeakObjectPtr<UKzDialogueAsset> Asset;
	TSharedPtr<SVerticalBox> Rows;

	void Refresh();
};