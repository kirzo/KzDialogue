// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Pin factory that detects the LineId pin on UK2Node_PlayDialogueLine and replaces
 * its default value widget with a custom dropdown showing the lines available in
 * the literal asset assigned to the node's Asset pin.
 *
 * When the Asset pin is connected, the factory falls back to the default GUID pin
 * widget (raw text input).
 */
class FKzDialogueLinePinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
};