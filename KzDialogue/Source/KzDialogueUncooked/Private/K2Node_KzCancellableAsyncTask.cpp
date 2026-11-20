// Copyright 2026 kirzo

#include "K2Node_KzCancellableAsyncTask.h"
#include "KzDialogueAsyncActions.h"

#include "K2Node_CallFunction.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "KismetCompiler.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_KzCancellableAsyncTask"

const FName UK2Node_KzCancellableAsyncTask::PN_Stop(TEXT("Stop"));
const FName UK2Node_KzCancellableAsyncTask::PN_Interrupt(TEXT("Interrupt"));

void UK2Node_KzCancellableAsyncTask::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	UEdGraphPin* StopPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, PN_Stop);
	StopPin->PinToolTip = TEXT("Stops the dialogue gracefully and resolves the action as Cancelled.");

	UEdGraphPin* InterruptPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, PN_Interrupt);
	InterruptPin->PinToolTip = TEXT("Interrupts the dialogue immediately and resolves the action as Cancelled.");

	// Exec inputs should sit above the data inputs: move them right after the activation exec pin.
	if (UEdGraphPin* ExecPin = FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
	{
		Pins.Remove(StopPin);
		Pins.Remove(InterruptPin);
		const int32 ExecIndex = Pins.IndexOfByKey(ExecPin);
		Pins.Insert(StopPin, ExecIndex + 1);
		Pins.Insert(InterruptPin, ExecIndex + 2);
	}
}

void UK2Node_KzCancellableAsyncTask::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	// Mirrors UK2Node_BaseAsyncTask::ExpandNode, but assigns the created proxy to a local variable so
	// the Stop/Interrupt exec inputs can act on the same instance (the factory return value is only
	// valid on the activation path). Delegate output pins are still wired by HandleDelegates.
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	bool bIsErrorFree = true;

	UK2Node_CallFunction* CallCreateProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallCreateProxyObjectNode->FunctionReference.SetExternalMember(ProxyFactoryFunctionName, ProxyFactoryClass);
	CallCreateProxyObjectNode->AllocateDefaultPins();
	if (CallCreateProxyObjectNode->GetTargetFunction() == nullptr)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MissingFactory", "KzCancellableAsyncTask: missing proxy factory function for @@").ToString(), this);
		return;
	}

	// Activation exec -> factory exec.
	bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Execute), *CallCreateProxyObjectNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute)).CanSafeConnect();

	// Move factory input data pins. Exec pins are skipped, so the Stop/Interrupt inputs are untouched here.
	for (UEdGraphPin* CurrentPin : Pins)
	{
		if (FBaseAsyncTaskHelper::ValidDataPin(CurrentPin, EGPD_Input))
		{
			UEdGraphPin* DestPin = CallCreateProxyObjectNode->FindPin(CurrentPin->PinName);
			bIsErrorFree &= DestPin && CompilerContext.MovePinLinksToIntermediate(*CurrentPin, *DestPin).CanSafeConnect();
		}
	}

	UEdGraphPin* const FactoryReturnPin = CallCreateProxyObjectNode->GetReturnValuePin();
	check(FactoryReturnPin);
	UEdGraphPin* OutputAsyncTaskProxy = FindPin(FBaseAsyncTaskHelper::GetAsyncTaskProxyName());
	bIsErrorFree &= !OutputAsyncTaskProxy || CompilerContext.MovePinLinksToIntermediate(*OutputAsyncTaskProxy, *FactoryReturnPin).CanSafeConnect();

	// Default the factory's WorldContext pin to self when left unconnected (UK2Node_BaseAsyncTask does
	// this via a private helper we cannot reach).
	if (UEdGraphPin* WorldContextPin = CallCreateProxyObjectNode->FindPin(TEXT("WorldContextObject")))
	{
		if (WorldContextPin->LinkedTo.Num() == 0)
		{
			UK2Node_Self* SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(this, SourceGraph);
			SelfNode->AllocateDefaultPins();
			bIsErrorFree &= Schema->TryCreateConnection(SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), WorldContextPin);
		}
	}

	// Store the proxy in a local variable shared by every exec path.
	UK2Node_TemporaryVariable* ProxyVar = CompilerContext.SpawnInternalVariable(this, UEdGraphSchema_K2::PC_Object, NAME_None, ProxyClass);
	UEdGraphPin* const ProxyObjectPin = ProxyVar->GetVariablePin();

	UK2Node_AssignmentStatement* AssignProxy = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
	AssignProxy->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(ProxyObjectPin, AssignProxy->GetVariablePin());
	AssignProxy->NotifyPinConnectionListChanged(AssignProxy->GetVariablePin());
	bIsErrorFree &= Schema->TryCreateConnection(FactoryReturnPin, AssignProxy->GetValuePin());
	bIsErrorFree &= Schema->TryCreateConnection(CallCreateProxyObjectNode->FindPinChecked(UEdGraphSchema_K2::PN_Then), AssignProxy->GetExecPin());

	UEdGraphPin* LastThenPin = AssignProxy->GetThenPin();

	// Gather delegate output params -> local variables (engine logic).
	TArray<FBaseAsyncTaskHelper::FOutputPinAndLocalVariable> VariableOutputs;
	bool bPassedFactoryOutputs = false;
	for (UEdGraphPin* CurrentPin : Pins)
	{
		if ((OutputAsyncTaskProxy != CurrentPin) && FBaseAsyncTaskHelper::ValidDataPin(CurrentPin, EGPD_Output))
		{
			if (!bPassedFactoryOutputs)
			{
				UEdGraphPin* DestPin = CallCreateProxyObjectNode->FindPin(CurrentPin->PinName);
				bIsErrorFree &= DestPin && CompilerContext.MovePinLinksToIntermediate(*CurrentPin, *DestPin).CanSafeConnect();
			}
			else
			{
				const FEdGraphPinType& PinType = CurrentPin->PinType;
				UK2Node_TemporaryVariable* TempVarOutput = CompilerContext.SpawnInternalVariable(this, PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get(), PinType.ContainerType, PinType.PinValueType);
				bIsErrorFree &= TempVarOutput->GetVariablePin() && CompilerContext.MovePinLinksToIntermediate(*CurrentPin, *TempVarOutput->GetVariablePin()).CanSafeConnect();
				VariableOutputs.Add(FBaseAsyncTaskHelper::FOutputPinAndLocalVariable(CurrentPin, TempVarOutput));
			}
		}
		else if (!bPassedFactoryOutputs && CurrentPin && CurrentPin->Direction == EGPD_Output)
		{
			bPassedFactoryOutputs = (CurrentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) && (CurrentPin->PinName != UEdGraphSchema_K2::PN_Then);
		}
	}

	// Validate the proxy on the activation path before binding/activating.
	UK2Node_CallFunction* IsValidFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	IsValidFuncNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid), UKismetSystemLibrary::StaticClass());
	IsValidFuncNode->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(ProxyObjectPin, IsValidFuncNode->FindPinChecked(TEXT("Object")));

	UK2Node_IfThenElse* ValidateProxyNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	ValidateProxyNode->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(IsValidFuncNode->GetReturnValuePin(), ValidateProxyNode->GetConditionPin());
	bIsErrorFree &= Schema->TryCreateConnection(LastThenPin, ValidateProxyNode->GetExecPin());
	LastThenPin = ValidateProxyNode->GetThenPin();

	bIsErrorFree &= HandleDelegates(VariableOutputs, ProxyObjectPin, LastThenPin, SourceGraph, CompilerContext);

	// Activate.
	if (ProxyActivateFunctionName != NAME_None)
	{
		UK2Node_CallFunction* CallActivateProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		CallActivateProxyObjectNode->FunctionReference.SetExternalMember(ProxyActivateFunctionName, ProxyClass);
		CallActivateProxyObjectNode->AllocateDefaultPins();
		bIsErrorFree &= Schema->TryCreateConnection(ProxyObjectPin, Schema->FindSelfPin(*CallActivateProxyObjectNode, EGPD_Input));
		bIsErrorFree &= Schema->TryCreateConnection(LastThenPin, CallActivateProxyObjectNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		LastThenPin = CallActivateProxyObjectNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);
	}

	// Route the original Then downstream; the Else (invalid proxy) skips straight to it too.
	if (UEdGraphPin* OriginalThenPin = FindPin(UEdGraphSchema_K2::PN_Then))
	{
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*OriginalThenPin, *LastThenPin).CanSafeConnect();
	}
	bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*LastThenPin, *ValidateProxyNode->GetElsePin()).CanSafeConnect();

	// Stop / Interrupt exec inputs act on the stored proxy.
	ExpandCancelExecPin(CompilerContext, SourceGraph, PN_Stop, GET_FUNCTION_NAME_CHECKED(UKzAsyncDialogueAction, Stop), ProxyObjectPin, bIsErrorFree);
	ExpandCancelExecPin(CompilerContext, SourceGraph, PN_Interrupt, GET_FUNCTION_NAME_CHECKED(UKzAsyncDialogueAction, Interrupt), ProxyObjectPin, bIsErrorFree);

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("InternalError", "KzCancellableAsyncTask: internal connection error in @@").ToString(), this);
	}

	BreakAllNodeLinks();
}

void UK2Node_KzCancellableAsyncTask::ExpandCancelExecPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, FName CancelPinName, FName ProxyFunctionName, UEdGraphPin* ProxyObjectPin, bool& bIsErrorFree)
{
	UEdGraphPin* CancelPin = FindPin(CancelPinName, EGPD_Input);
	if (!CancelPin || CancelPin->LinkedTo.Num() == 0)
	{
		return;
	}

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// Guard against a null / already-destroyed proxy before calling the cancel function.
	UK2Node_CallFunction* IsValidNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	IsValidNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid), UKismetSystemLibrary::StaticClass());
	IsValidNode->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(ProxyObjectPin, IsValidNode->FindPinChecked(TEXT("Object")));

	UK2Node_IfThenElse* Branch = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	Branch->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(IsValidNode->GetReturnValuePin(), Branch->GetConditionPin());
	bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*CancelPin, *Branch->GetExecPin()).CanSafeConnect();

	UK2Node_CallFunction* CallCancelNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallCancelNode->FunctionReference.SetExternalMember(ProxyFunctionName, UKzAsyncDialogueAction::StaticClass());
	CallCancelNode->AllocateDefaultPins();
	bIsErrorFree &= Schema->TryCreateConnection(ProxyObjectPin, Schema->FindSelfPin(*CallCancelNode, EGPD_Input));
	bIsErrorFree &= Schema->TryCreateConnection(Branch->GetThenPin(), CallCancelNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
}

#undef LOCTEXT_NAMESPACE