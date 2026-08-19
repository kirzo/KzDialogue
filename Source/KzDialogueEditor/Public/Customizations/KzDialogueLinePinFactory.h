// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Pin factory for the plugin's dialogue-aware pins.
 *
 * LineId pins of our K2Nodes get a dropdown of the lines available in the literal asset
 * assigned to the node's Asset pin (default GUID text input when the Asset pin is
 * connected).
 *
 * Named-token pins get the token browser instead of a blind text box. Functions opt in
 * through UFUNCTION metadata: KzTokenPin names the token parameter. Name pins browse base
 * tokens only; string pins ("Token:part" slots) browse the parts too.
 */
class FKzDialogueLinePinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
};