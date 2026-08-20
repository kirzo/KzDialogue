// Copyright 2026 kirzo

#include "KzDialogueFunctionLibrary.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"
#include "KzNamedTokenSubsystem.h"
#include "KzSpeakerAsset.h"
#include "Engine/World.h"

static UKzDialogueSubsystem* GetDialogueSubsystem(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject)) { return nullptr; }
	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
}

bool UKzDialogueFunctionLibrary::IsDialogueSpeakerValid(const FKzDialogueSpeaker& Speaker)
{
	return Speaker.IsValid();
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::GetDialoguePlayer(const UObject* WorldContextObject, FGameplayTag InChannel, bool bCreateIfNotFound)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub))
	{
		return nullptr;
	}
	return bCreateIfNotFound ? Sub->GetOrCreatePlayer(InChannel) : Sub->FindPlayer(InChannel);
}

UKzDialogueAssetSession* UKzDialogueFunctionLibrary::PlayDialogueAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset,
	FGameplayTag Channel, bool bStartImmediately, EKzDialogueAdvanceMode AdvanceMode)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayAsset(Asset, Channel, bStartImmediately, AdvanceMode) : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineFromAsset(const UObject* WorldContextObject, UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayAssetLine(Asset, LineId, Channel, Priority, bStartImmediately) : nullptr;
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLine(const UObject* WorldContextObject, const FKzDialogueLineRef& Ref, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	if (!Ref.IsValid()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return nullptr; }

	UKzDialogueAsset* Loaded = Ref.Asset.LoadSynchronous();
	if (!Loaded) { return nullptr; }

	return Sub->PlayAssetLine(Loaded, Ref.LineId, Channel, Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineDirect(const UObject* WorldContextObject, const FKzDialogueLine& Line, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayLine(Line, Channel, Priority, bStartImmediately) : nullptr;
}

bool UKzDialogueFunctionLibrary::TryResolveDialogueLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine)
{
	return Ref.TryResolve(OutLine);
}

UKzDialogueAssetSession* UKzDialogueFunctionLibrary::PlayDialogueLineList(const UObject* WorldContextObject, const FKzDialogueLineList& List, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	if (!List.IsValid()) { return nullptr; }

	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return nullptr; }

	UKzDialogueAsset* Loaded = List.Asset.LoadSynchronous();
	if (!Loaded) { return nullptr; }

	return Sub->PlayLineList(Loaded, List.LineIds, Channel, Priority, bStartImmediately);
}

UKzDialoguePlayer* UKzDialogueFunctionLibrary::PlayDialogueLineRefs(const UObject* WorldContextObject, const TArray<FKzDialogueLineRef>& Refs, FGameplayTag Channel, int32 Priority, bool bStartImmediately)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	return IsValid(Sub) ? Sub->PlayLineRefs(Refs, Channel, Priority, bStartImmediately) : nullptr;
}

bool UKzDialogueFunctionLibrary::TryResolveDialogueLineList(const FKzDialogueLineList& List, TArray<FKzDialogueLine>& OutLines)
{
	return List.TryResolveAll(OutLines);
}

void UKzDialogueFunctionLibrary::GetDialogueLineRefsFromList(const FKzDialogueLineList& List, TArray<FKzDialogueLineRef>& OutRefs)
{
	List.GetLineRefs(OutRefs);
}

bool UKzDialogueFunctionLibrary::IsDialogueChannelPlaying(const UObject* WorldContextObject, FGameplayTag Channel)
{
	UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject);
	if (!IsValid(Sub)) { return false; }

	TArray<UKzDialoguePlayer*> Matching;
	Sub->GetPlayersInScope(Channel, Matching);
	for (const UKzDialoguePlayer* Player : Matching)
	{
		if (Player->IsPlaying()) { return true; }
	}
	return false;
}

void UKzDialogueFunctionLibrary::StopDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->StopChannel(Channel);
	}
}

void UKzDialogueFunctionLibrary::StopAllDialogues(const UObject* WorldContextObject)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->StopAll();
	}
}

void UKzDialogueFunctionLibrary::InterruptDialogueChannel(const UObject* WorldContextObject, FGameplayTag Channel)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->InterruptChannel(Channel);
	}
}

void UKzDialogueFunctionLibrary::InterruptAllDialogues(const UObject* WorldContextObject)
{
	if (UKzDialogueSubsystem* Sub = GetDialogueSubsystem(WorldContextObject))
	{
		Sub->InterruptAll();
	}
}

FGameplayTagContainer UKzDialogueFunctionLibrary::GetLineTags(const FKzDialogueLine& Line)
{
	return Line.Tags;
}

bool UKzDialogueFunctionLibrary::LineHasTag(const FKzDialogueLine& Line, FGameplayTag Tag, bool bExact)
{
	return bExact ? Line.Tags.HasTagExact(Tag) : Line.Tags.HasTag(Tag);
}

bool UKzDialogueFunctionLibrary::LineHasAnyTags(const FKzDialogueLine& Line, const FGameplayTagContainer& Tags, bool bExact)
{
	return bExact ? Line.Tags.HasAnyExact(Tags) : Line.Tags.HasAny(Tags);
}

bool UKzDialogueFunctionLibrary::LineHasAllTags(const FKzDialogueLine& Line, const FGameplayTagContainer& Tags, bool bExact)
{
	return bExact ? Line.Tags.HasAllExact(Tags) : Line.Tags.HasAll(Tags);
}

void UKzDialogueFunctionLibrary::SetLineTextArgument(FKzDialogueLine& Line, FName Name, FText Value)
{
	Line.FormatArguments.Add(Name.ToString(), FFormatArgumentValue(Value));
}

void UKzDialogueFunctionLibrary::SetLineNumberArgument(FKzDialogueLine& Line, FName Name, double Value)
{
	Line.FormatArguments.Add(Name.ToString(), FFormatArgumentValue(Value));
}

FText UKzDialogueFunctionLibrary::GetLineFormattedText(const FKzDialogueLine& Line)
{
	return Line.GetFormattedText();
}

bool UKzDialogueFunctionLibrary::TryResolveNamedText(const UObject* WorldContextObject, const FString& TokenAndModifier, FText& OutText)
{
	FString Token = TokenAndModifier;
	FString Modifier;
	TokenAndModifier.Split(TEXT(":"), &Token, &Modifier);

	const UKzNamedTokenSubsystem* Tokens = UKzNamedTokenSubsystem::Get(WorldContextObject);
	const FSoftObjectPath* Path = Tokens ? Tokens->FindNamedAssetPath(FName(*Token)) : nullptr;
	if (!Path) { return false; }

	// Small data assets: a synchronous load on first resolve is fine; later resolves hit memory.
	const UKzNamedAsset* Thing = Cast<UKzNamedAsset>(Path->TryLoad());
	if (!Thing) { return false; }

	OutText = Thing->ResolveName(Modifier.IsEmpty() ? NAME_None : FName(*Modifier), Tokens->FindOverride(FName(*Token)));
	return true;
}

FText UKzDialogueFunctionLibrary::ResolveNamedText(const UObject* WorldContextObject, const FString& TokenAndModifier)
{
	FText Result;
	TryResolveNamedText(WorldContextObject, TokenAndModifier, Result);
	return Result;
}

FText UKzDialogueFunctionLibrary::ResolveTextTokens(const UObject* WorldContextObject, FText Text)
{
	TArray<FString> ArgumentNames;
	FTextFormat(Text).GetFormatArgumentNames(ArgumentNames);
	if (ArgumentNames.Num() == 0) { return Text; }

	// Named-asset tier only: unclaimed arguments are left out of the map, and the formatter
	// keeps their placeholders literal for the caller's own formatting pass.
	FFormatNamedArguments Arguments;
	for (const FString& ArgumentName : ArgumentNames)
	{
		FFormatArgumentValue Value;
		if (TryResolveNamedArgument(WorldContextObject, ArgumentName, Value))
		{
			Arguments.Add(ArgumentName, Value);
		}
	}
	if (Arguments.Num() == 0) { return Text; }

	return FText::Format(FTextFormat(Text), Arguments);
}

bool UKzDialogueFunctionLibrary::TryResolveNamedArgument(const UObject* WorldContextObject, const FString& TokenAndModifier, FFormatArgumentValue& OutValue)
{
	FString Token = TokenAndModifier;
	FString Modifier;
	TokenAndModifier.Split(TEXT(":"), &Token, &Modifier);

	if (Modifier.Equals(TEXT("gender"), ESearchCase::IgnoreCase))
	{
		const UKzNamedTokenSubsystem* Tokens = UKzNamedTokenSubsystem::Get(WorldContextObject);
		const FSoftObjectPath* Path = Tokens ? Tokens->FindNamedAssetPath(FName(*Token)) : nullptr;
		if (const UKzSpeakerAsset* Speaker = Path ? Cast<UKzSpeakerAsset>(Path->TryLoad()) : nullptr)
		{
			const FKzNamedTokenOverride* Override = Tokens->FindOverride(FName(*Token));
			const EKzGender Gender = (Override && Override->bOverrideGender) ? Override->Gender : Speaker->Gender;

			// Unspecified maps to Neuter, the |gender() fallback form.
			const ETextGender TextGender =
				Gender == EKzGender::Masculine ? ETextGender::Masculine :
				Gender == EKzGender::Feminine ? ETextGender::Feminine : ETextGender::Neuter;
			OutValue = FFormatArgumentValue(TextGender);
			return true;
		}
	}

	FText Text;
	if (TryResolveNamedText(WorldContextObject, TokenAndModifier, Text))
	{
		OutValue = FFormatArgumentValue(Text);
		return true;
	}
	return false;
}