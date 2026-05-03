// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/KzPropertyStackRowCustomizer.h"
#include "UObject/WeakObjectPtr.h"

class UAudioComponent;
class USoundBase;

/**
 * Per-row customizer for FKzDialogueLine entries inside the dialogue asset editor.
 *
 * Adds:
 *   - A leading icon indicating audio status (none / present / broken).
 *   - A trailing play/stop button that auditions the line audio without entering PIE.
 *   - A custom display label that combines speaker and text (delegated to the line itself).
 *   - A tooltip surfacing the speaker and the full text without truncation.
 *
 * Audio playback uses the editor's preview audio device. Only one line can play at a time;
 * pressing play on a different line stops the current preview.
 */
class FKzDialogueLineRowCustomizer : public FKzPropertyStackRowCustomizer
{
public:
	virtual ~FKzDialogueLineRowCustomizer() override;

	//~ Begin FKzPropertyStackRowCustomizer
	virtual void OnRegister(FAssetEditorToolkit* InHostEditor) override;
	virtual void OnUnregister() override;

	virtual TSharedRef<SWidget> BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual TSharedRef<SWidget> BuildTrailingWidget(TSharedPtr<IPropertyHandle> Handle) override;
	virtual FText GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const override;
	virtual FText GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const override;
	//~ End FKzPropertyStackRowCustomizer

private:
	/** Returns the brush used for the audio status indicator (left of the label). */
	const FSlateBrush* GetAudioStatusBrush(TSharedPtr<IPropertyHandle> Handle) const;

	/**
	 * Returns the brush for the play/stop trailing button depending 
	 * on whether this particular row is currently auditioning.
	 */
	const FSlateBrush* GetPlayButtonBrush(TSharedPtr<IPropertyHandle> Handle) const;

	/** Whether the play button should be enabled (only when the line has audio). */
	bool IsPlayButtonEnabled(TSharedPtr<IPropertyHandle> Handle) const;

	/** Click handler. Toggles audition for this row. */
	FReply OnPlayClicked(TSharedPtr<IPropertyHandle> Handle);

	/** Stop any active audition (called from OnUnregister and when switching rows). */
	void StopActiveAudition();

	/** True if the given handle is the one currently being auditioned. */
	bool IsAuditioning(TSharedPtr<IPropertyHandle> Handle) const;

	/** Resolve the line currently being edited from the row's property handle. */
	struct FKzDialogueLine* ResolveLine(TSharedPtr<IPropertyHandle> Handle) const;

	/**
	 * Audio component used for editor-side preview.
	 * Created lazily and freed in StopActiveAudition / OnUnregister.
	 */
	TWeakObjectPtr<UAudioComponent> ActivePreviewAudio;

	/**
	 * GUID of the line currently being auditioned. Used to track which
	 * row should show the "stop" icon and to ignore stale callbacks.
	 */
	FGuid ActiveLineId;
};