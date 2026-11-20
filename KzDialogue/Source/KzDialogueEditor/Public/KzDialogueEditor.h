// Copyright 2026 kirzo

#pragma once

#include "KzLibEditorModule_Base.h"

class UKzDialogueAsset;

class FKzDialogueEditorModule : public FKzLibEditorModule_Base
{
public:
	/** Opens (or focuses) Asset's editor with the line or alias EntryId selected in its tab; an invalid id just opens it. */
	static void OpenAssetAtEntry(UKzDialogueAsset* Asset, const FGuid& EntryId);

private:
	FDelegateHandle DialogueTrackEditorHandle;
	FDelegateHandle NamedTokenRenamedHandle;
	FDelegateHandle BeginPIEHandle;
	TSharedPtr<class FKzDialogueLinePinFactory> LinePinFactory;

protected:
	virtual void OnStartupModule() override;
	virtual void OnShutdownModule() override;
};