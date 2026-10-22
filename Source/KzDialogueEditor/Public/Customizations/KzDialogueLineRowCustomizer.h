// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/KzPropertyStackRowCustomizer.h"

/**
 * Per-row customizer for FKzDialogueLine entries inside the dialogue asset editor: an audio status
 * icon, a play/stop button auditioning the line, and a speaker + text label and tooltip.
 */
class FKzDialogueLineRowCustomizer : public FKzPropertyStackRowCustomizer
{
public:
	//~ Begin FKzPropertyStackRowCustomizer
	virtual TSharedRef<SWidget> BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual TSharedRef<SWidget> BuildTrailingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual FText GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual FText GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual bool TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const override;
	//~ End FKzPropertyStackRowCustomizer

private:
	const FSlateBrush* GetAudioStatusBrush(TSharedPtr<IPropertyHandle> Handle) const;
	bool IsAuditioning(TSharedPtr<IPropertyHandle> Handle) const;
	FReply OnPlayClicked(TSharedPtr<IPropertyHandle> Handle);

	struct FKzDialogueLine* ResolveLine(TSharedPtr<IPropertyHandle> Handle) const;
};