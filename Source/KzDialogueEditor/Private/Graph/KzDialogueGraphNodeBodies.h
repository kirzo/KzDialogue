// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UKzDialogueGraph;

/** The bodies dialogue graph nodes show under their title (a line's speaker and text, a choice's options, a branch's conditions, any node's Only If, a Go To's entry), and the nodes' double clicks. */
namespace KzDialogueGraphNodeBodies
{
	void Register();
	void Unregister();

	/** The entry names of an editor graph, as its nodes stand now. */
	TArray<FName> GatherEntryNames(const UEdGraph& Graph);

	/** The entry names of a graph asset: from its editor graph when it has one, else from its last compile. */
	TArray<FName> GatherEntryNames(const UKzDialogueGraph& Graph);
}