// Copyright 2026 kirzo

#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"
#include "KzNamedTokenSubsystem.h"
#include "KzSpeakerAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/AutomationTest.h"

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<bool> CVarMarkMissingTranslations(
	TEXT("Kz.Dialogue.MarkMissingTranslations"), true,
	TEXT("Prefix line texts with no translation for the active culture with \"[!] \", so QA spots them. Not in shipping builds."));

/** True when Text is localizable and the active culture has no current translation of it, so it falls back to its source. */
static bool IsMissingTranslation(const FText& Text)
{
	if (Text.IsEmpty() || Text.IsCultureInvariant()) { return false; }

	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
	const TOptional<FString> Key = FTextInspector::GetKey(Text);
	const FString* Source = FTextInspector::GetSourceString(Text);
	if (!Namespace.IsSet() || !Key.IsSet() || !Source) { return false; }

	// The native culture (and its variants: en-US for en) shows its own source by design.
	FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
	const FString NativeCulture = Manager.GetNativeCultureName(ELocalizedTextSourceCategory::Game);
	if (NativeCulture.IsEmpty() || FInternationalization::Get().GetCurrentLanguage()->GetPrioritizedParentCultureNames().Contains(NativeCulture)) { return false; }

	// GetDisplayString strips the editor's package namespace and treats a translation of an older source as missing.
	return !Manager.GetDisplayString(FTextKey(Namespace.GetValue()), FTextKey(Key.GetValue()), Source).IsValid();
}
#endif

namespace
{
	/** Marks where a lowercase text argument begins inside a formatted string. Private use, never in authored text. */
	constexpr TCHAR ArgumentMark = 0xE000;

	bool IsSentenceStart(const FString& Before)
	{
		int32 Index = Before.Len() - 1;
		while (Index >= 0 && (FChar::IsWhitespace(Before[Index]) || FCString::Strchr(TEXT("\"'([\x201C\x2018\x00AB\x00BF\x00A1\x2014"), Before[Index])))
		{
			--Index;
		}
		if (Index < 0) { return true; }

		const TCHAR Last = Before[Index];
		return Last == TEXT('!') || Last == TEXT('?') || (Last == TEXT('.') && (Index == 0 || Before[Index - 1] != TEXT('.')));
	}
}

FText KzFormatText(const FText& Pattern, const FFormatNamedArguments& Arguments)
{
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	if (!Settings || !Settings->bCapitalizeArgumentsAtSentenceStart)
	{
		return FText::Format(FTextFormat(Pattern), Arguments);
	}

	// Only an argument starting in lowercase ("van Dijk") can need it: every other text keeps its plain format.
	bool bAnyLowercase = false;
	FFormatNamedArguments Marked;
	for (const TPair<FString, FFormatArgumentValue>& Argument : Arguments)
	{
		const FString Value = Argument.Value.GetType() == EFormatArgumentType::Text ? Argument.Value.GetTextValue().ToString() : FString();
		if (!Value.IsEmpty() && FChar::IsLower(Value[0]))
		{
			bAnyLowercase = true;
			Marked.Add(Argument.Key, FText::AsCultureInvariant(FString::Chr(ArgumentMark) + Value));
		}
		else
		{
			Marked.Add(Argument.Key, Argument.Value);
		}
	}
	if (!bAnyLowercase)
	{
		return FText::Format(FTextFormat(Pattern), Arguments);
	}

	const FString Formatted = FText::Format(FTextFormat(Pattern), Marked).ToString();
	FString Out;
	for (int32 Index = 0; Index < Formatted.Len(); ++Index)
	{
		if (Formatted[Index] != ArgumentMark)
		{
			Out += Formatted[Index];
		}
		else if (Index + 1 < Formatted.Len() && IsSentenceStart(Out))
		{
			Out += FText::AsCultureInvariant(Formatted.Mid(++Index, 1)).ToUpper().ToString();
		}
	}
	return FText::AsCultureInvariant(Out);
}

FText FKzDialogueLine::GetFormattedText() const
{
	return FormatArguments.Num() > 0 ? KzFormatText(Text, FormatArguments) : Text;
}

FText FKzDialogueLine::GetDisplayText() const
{
#if !UE_BUILD_SHIPPING
	if (CVarMarkMissingTranslations.GetValueOnGameThread() && IsMissingTranslation(Text))
	{
		return FText::Format(INVTEXT("[!] {0}"), GetFormattedText());
	}
#endif
	return GetFormattedText();
}

float FKzDialogueLine::ResolveReadingDuration() const
{
	return ResolveReadingDuration(UKzDialogueSettings::GetReadingSpeedScale());
}

float FKzDialogueLine::ResolveReadingDuration(float ReadingSpeedScale) const
{
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const float Speed = Settings ? Settings->GetActiveReadingSpeed() * ReadingSpeedScale : 0.0f;
	if (Speed <= 0.0f) { return Settings ? Settings->DefaultDuration : 2.5f; }

	return FMath::Max(Settings->MinReadingDuration, GetFormattedText().ToString().TrimStartAndEnd().Len() / Speed);
}

float FKzDialogueLine::ResolveTotalDuration(float FullAudioDuration) const
{
	return ResolveTotalDuration(FullAudioDuration, UKzDialogueSettings::GetReadingSpeedScale());
}

float FKzDialogueLine::ResolveTotalDuration(float FullAudioDuration, float ReadingSpeedScale) const
{
	// Trimmed range when the line cuts a section out of the wave; full length otherwise.
	float End = FullAudioDuration;
	if (AudioEndTime > 0.0f)
	{
		End = FullAudioDuration > 0.0f ? FMath::Min(AudioEndTime, FullAudioDuration) : AudioEndTime;
	}
	const float AudioLength = FMath::Max(0.0f, End - FMath::Max(0.0f, AudioStartTime));

	float Length = 0.0f;
	switch (DurationMode)
	{
	case EKzLineDurationMode::Override:
		Length = Duration;
		break;
	case EKzLineDurationMode::ExtendAudio:
		Length = AudioLength + Duration;
		break;
	default:
		Length = Duration > 0.0f ? Duration : (AudioLength > 0.0f ? AudioLength : ResolveReadingDuration(ReadingSpeedScale));
		break;
	}

	// The audio lead extends the line: the text lives its resolved time plus the delay.
	return Length + FMath::Max(0.0f, AudioStartDelay);
}

FText FKzDialogueSpeaker::GetSubtitleName(const UObject* WorldContextObject) const
{
	if (!Asset)
	{
		return FText::GetEmpty();
	}

	const FKzNamedTokenOverride* Override = UKzNamedTokenSubsystem::FindOverrideFor(WorldContextObject, Asset->Token);
	const FText Resolved = Asset->ResolveName(NAME_None, Override);
	if (!Resolved.IsEmpty())
	{
		return Resolved;
	}

	// A freshly created asset with no name authored yet.
	return FText::FromString(FName::NameToDisplayString(Asset->GetName(), false));
}

FText FKzDialogueSpeaker::GetEditorLabel() const
{
	return Asset ? GetSubtitleName(nullptr) : NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
}

bool FKzDialogueLineRef::TryResolve(FKzDialogueLine& OutLine) const
{
	if (!IsValid()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineId, OutLine);
}

bool FKzDialogueLineList::TryResolve(int32 Index, FKzDialogueLine& OutLine) const
{
	if (!LineIds.IsValidIndex(Index)) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineIds[Index], OutLine);
}

bool FKzDialogueLineList::TryResolveAll(TArray<FKzDialogueLine>& OutLines) const
{
	OutLines.Reset();
	if (!Asset.IsValid() && Asset.IsNull()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	OutLines.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLine Line;
		if (Loaded->TryResolveLineOrAlias(Id, Line))
		{
			OutLines.Add(MoveTemp(Line));
		}
	}
	return OutLines.Num() > 0;
}

void FKzDialogueLineList::GetLineRefs(TArray<FKzDialogueLineRef>& OutRefs) const
{
	OutRefs.Reset();
	OutRefs.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLineRef Ref;
		Ref.Asset = Asset;
		Ref.LineId = Id;
		OutRefs.Add(MoveTemp(Ref));
	}
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKzFormatTextCapitalizeTest, "KzDialogue.Text.CapitalizeArguments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKzFormatTextCapitalizeTest::RunTest(const FString& Parameters)
{
	if (!UKzDialogueSettings::Get()->bCapitalizeArgumentsAtSentenceStart) { return true; }

	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Name"), FText::AsCultureInvariant(TEXT("van Dijk")));
	auto Format = [&Arguments](const TCHAR* Pattern) { return KzFormatText(FText::AsCultureInvariant(Pattern), Arguments).ToString(); };

	TestEqual(TEXT("Text start"), Format(TEXT("{Name} said it.")), TEXT("Van Dijk said it."));
	TestEqual(TEXT("After a period"), Format(TEXT("Read. {Name} said it.")), TEXT("Read. Van Dijk said it."));
	TestEqual(TEXT("Mid sentence"), Format(TEXT("Ask {Name}.")), TEXT("Ask van Dijk."));
	TestEqual(TEXT("After an ellipsis"), Format(TEXT("Our... {Name}.")), TEXT("Our... van Dijk."));
	TestEqual(TEXT("Opening quote"), Format(TEXT("Well. \"{Name}!\"")), TEXT("Well. \"Van Dijk!\""));
	return true;
}

#endif