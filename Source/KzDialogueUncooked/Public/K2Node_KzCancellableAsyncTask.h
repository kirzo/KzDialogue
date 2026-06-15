// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_KzCancellableAsyncTask.generated.h"

/**
 * Base for the dialogue async-task nodes that expose Stop and Interrupt exec inputs alongside the
 * activation exec. Unlike UK2Node_BaseAsyncTask, the expansion stores the created proxy in a local
 * variable so all three exec paths act on the same async-action instance (the factory return value
 * is only valid on the activation path); the delegate output pins are still wired by HandleDelegates.
 */
UCLASS(Abstract)
class KZDIALOGUEUNCOOKED_API UK2Node_KzCancellableAsyncTask : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** Exec input names. */
	static const FName PN_Stop;
	static const FName PN_Interrupt;

private:
	/** Expands one cancel exec input into IsValid(proxy) -> proxy->Function(). No-op when the pin is unconnected. */
	void ExpandCancelExecPin(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, FName CancelPinName, FName ProxyFunctionName, UEdGraphPin* ProxyObjectPin, bool& bIsErrorFree);
};