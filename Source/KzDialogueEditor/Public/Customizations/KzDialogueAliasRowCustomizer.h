// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/KzPropertyStackRowCustomizer.h"

/**
 * Per-row customizer for FKzDialogueAlias entries.
 *
 * Adds:
 *   - A leading icon (random/dice) signalling the alias resolves to one of N lines.
 *   - A custom display label combining the alias name and its line count.
 *   - Tooltip listing the first few resolved lines.
 */
class FKzDialogueAliasRowCustomizer : public FKzPropertyStackRowCustomizer
{
public:
	//~ Begin FKzPropertyStackRowCustomizer
	virtual TSharedRef<SWidget> BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual FText GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual FText GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual bool TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const override;
	//~ End FKzPropertyStackRowCustomizer

private:
	struct FKzDialogueAlias* ResolveAlias(TSharedPtr<IPropertyHandle> Handle) const;
};