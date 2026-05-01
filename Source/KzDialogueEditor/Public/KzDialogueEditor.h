// Copyright 2026 kirzo

#pragma once

#include "KzLibEditorModule_Base.h"

class FKzDialogueEditorModule : public FKzLibEditorModule_Base
{
private:
	FDelegateHandle DialogueTrackEditorHandle;

protected:
	virtual void OnStartupModule() override;
	virtual void OnShutdownModule() override;
};