// Copyright 2026 kirzo

#include "Script/KzFountainParser.h"
#include "Misc/AutomationTest.h"

namespace
{
	FString StripBoneyard(const FString& In)
	{
		FString Out;
		int32 Pos = 0;
		while (Pos < In.Len())
		{
			const int32 Open = In.Find(TEXT("/*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const int32 Close = Open == INDEX_NONE ? INDEX_NONE : In.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 2);
			if (Close == INDEX_NONE)
			{
				Out += In.Mid(Pos);
				break;
			}
			Out += In.Mid(Pos, Open - Pos);
			Pos = Close + 2;
		}
		return Out;
	}

	/** Line without the spans between Open and Close, whose insides go to OutNotes. */
	FString ExtractSpans(const FString& Line, const TCHAR* Open, const TCHAR* Close, TArray<FString>& OutNotes)
	{
		const int32 OpenLen = FCString::Strlen(Open);
		const int32 CloseLen = FCString::Strlen(Close);
		FString Out;
		int32 Pos = 0;
		while (Pos < Line.Len())
		{
			const int32 Start = Line.Find(Open, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const int32 End = Start == INDEX_NONE ? INDEX_NONE : Line.Find(Close, ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + OpenLen);
			if (End == INDEX_NONE)
			{
				Out += Line.Mid(Pos);
				break;
			}
			Out += Line.Mid(Pos, Start - Pos);
			OutNotes.Add(Line.Mid(Start + OpenLen, End - Start - OpenLen).TrimStartAndEnd());
			Pos = End + CloseLen;
		}
		return Out.Replace(TEXT("  "), TEXT(" ")).TrimStartAndEnd();
	}

	/** Spoken text without its directions, whole-line or inline ("It was HIM. (emphatic) HIM."), which go to OutNotes. */
	FString ExtractDirections(const FString& Line, TArray<FString>& OutNotes)
	{
		const int32 NotesBefore = OutNotes.Num();
		FString Out = ExtractSpans(Line, TEXT("("), TEXT(")"), OutNotes);
		if (OutNotes.Num() == NotesBefore) { return Out; }

		// A direction between em dashes leaves one dash, and one before a period leaves no space before it.
		Out.ReplaceInline(TEXT("\x2014\x2014"), TEXT("\x2014"));
		for (const TCHAR* Punctuation : { TEXT("."), TEXT(","), TEXT("?"), TEXT("!") })
		{
			Out.ReplaceInline(*FString::Printf(TEXT(" %s"), Punctuation), Punctuation);
		}
		return Out;
	}

	/** Removes '*' emphasis marks; "\*" stays as a literal '*'. */
	FString StripEmphasis(const FString& In)
	{
		FString Out;
		for (int32 Index = 0; Index < In.Len(); ++Index)
		{
			if (In[Index] == TEXT('\\') && Index + 1 < In.Len() && In[Index + 1] == TEXT('*'))
			{
				Out += TEXT('*');
				++Index;
			}
			else if (In[Index] != TEXT('*'))
			{
				Out += In[Index];
			}
		}
		return Out;
	}

	bool IsTitlePageKey(const FString& Line)
	{
		int32 Colon;
		if (!Line.FindChar(TEXT(':'), Colon) || Colon == 0) { return false; }
		for (int32 Index = 0; Index < Colon; ++Index)
		{
			if (!FChar::IsAlpha(Line[Index]) && Line[Index] != TEXT(' ')) { return false; }
		}
		return true;
	}

	bool IsSceneHeading(const FString& Line)
	{
		for (const TCHAR* Prefix : { TEXT("INT"), TEXT("EXT"), TEXT("EST"), TEXT("I/E") })
		{
			const int32 Len = FCString::Strlen(Prefix);
			if (Line.StartsWith(Prefix) && Line.Len() > Len && (Line[Len] == TEXT('.') || Line[Len] == TEXT(' ') || Line[Len] == TEXT('/')))
			{
				return true;
			}
		}
		return false;
	}

	/** A character cue: forced with '@', or an uppercase name (extensions aside) that is no heading or transition. */
	bool ParseCue(const FString& Line, FString& OutCue)
	{
		FString Name = Line;
		const bool bForced = Name.RemoveFromStart(TEXT("@"));
		Name.RemoveFromEnd(TEXT("^"));
		int32 Paren;
		if (Name.FindChar(TEXT('('), Paren))
		{
			Name.LeftInline(Paren);
		}
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty()) { return false; }

		if (!bForced)
		{
			if (FCString::Strchr(TEXT("!>.=~[#"), Name[0]) || IsSceneHeading(Name) || Name.EndsWith(TEXT("TO:"))) { return false; }

			bool bHasLetter = false;
			for (const TCHAR C : Name)
			{
				if (FChar::IsLower(C)) { return false; }
				bHasLetter |= FChar::IsAlpha(C);
			}
			if (!bHasLetter) { return false; }
		}

		OutCue = Name;
		return true;
	}
}

TArray<FKzFountainSection> KzFountain::Parse(const FString& Script, const FString& DefaultTitle)
{
	TArray<FString> Lines;
	StripBoneyard(Script.Replace(TEXT("\r"), TEXT(""))).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
	auto IsBlank = [&Lines](int32 Index) { return !Lines.IsValidIndex(Index) || Lines[Index].TrimStartAndEnd().IsEmpty(); };

	int32 Index = 0;
	if (Lines.Num() > 0 && IsTitlePageKey(Lines[0]))
	{
		while (!IsBlank(Index)) { ++Index; }
	}

	// Cues written the strict way, after a blank line: a later line naming one is a cue even glued to the line before.
	TSet<FString> KnownCues;
	for (int32 LineIndex = Index; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Cue;
		if (IsBlank(LineIndex - 1) && !IsBlank(LineIndex + 1) && ParseCue(Lines[LineIndex].TrimStartAndEnd(), Cue))
		{
			KnownCues.Add(NormalizeName(Cue));
		}
	}

	TArray<FKzFountainSection> Sections;
	auto CurrentSection = [&]() -> FKzFountainSection&
	{
		if (Sections.IsEmpty()) { Sections.AddDefaulted_GetRef().Title = DefaultTitle; }
		return Sections.Last();
	};

	bool bInBlock = false;
	TArray<FString> TextParts;
	TArray<FString> NoteParts;
	auto CloseBlock = [&]()
	{
		if (!bInBlock) { return; }
		FKzFountainBlock& Block = Sections.Last().Blocks.Last();
		Block.Text = FString::Join(TextParts, TEXT(" "));
		Block.Notes = FString::Join(NoteParts, TEXT("; "));
		TextParts.Reset();
		NoteParts.Reset();
		bInBlock = false;
	};

	for (; Index < Lines.Num(); ++Index)
	{
		if (IsBlank(Index))
		{
			CloseBlock();
			continue;
		}

		TArray<FString> Notes;
		const FString Content = ExtractSpans(Lines[Index].TrimStartAndEnd(), TEXT("[["), TEXT("]]"), Notes);

		FString Cue;
		const bool bCue = !IsBlank(Index + 1) && ParseCue(Content, Cue)
			&& (IsBlank(Index - 1) || KnownCues.Contains(NormalizeName(Cue)) || Lines[Index + 1].TrimStart().StartsWith(TEXT("(")));

		// A heading or a cue glued to a dialogue still ends it.
		if (bInBlock && !bCue && !Content.StartsWith(TEXT("#")))
		{
			NoteParts.Append(Notes);
			const FString Text = ExtractDirections(ExtractSpans(Content, TEXT("["), TEXT("]"), NoteParts), NoteParts);
			if (!Text.IsEmpty())
			{
				TextParts.Add(StripEmphasis(Text.StartsWith(TEXT("~")) ? Text.RightChop(1).TrimStart() : Text));
			}
			continue;
		}
		CloseBlock();

		if (Content.StartsWith(TEXT("#")))
		{
			int32 Depth = 0;
			while (Depth < Content.Len() && Content[Depth] == TEXT('#')) { ++Depth; }
			const FString Title = Content.RightChop(Depth).TrimStartAndEnd();

			// Scene headings written as sections ("##EXT. FOREST - DAY") stay inside the section.
			if (!IsSceneHeading(Title))
			{
				Sections.AddDefaulted_GetRef().Title = Title;
			}
		}

		TArray<FString> OtherNotes;
		for (const FString& Note : Notes)
		{
			FString AssetName = Note;
			if (AssetName.RemoveFromStart(TEXT("asset:")))
			{
				CurrentSection().AssetName = AssetName.TrimStartAndEnd();
			}
			else
			{
				OtherNotes.Add(Note);
			}
		}

		if (bCue)
		{
			CurrentSection().Blocks.AddDefaulted_GetRef().Cue = Cue;
			NoteParts = OtherNotes;
			bInBlock = true;
		}
	}
	CloseBlock();

	Sections.RemoveAll([](const FKzFountainSection& Section) { return Section.Blocks.IsEmpty(); });
	return Sections;
}

FString KzFountain::NormalizeName(const FString& Name)
{
	FString Out;
	for (const TCHAR C : Name)
	{
		if (FChar::IsAlnum(C)) { Out += FChar::ToUpper(C); }
	}
	return Out;
}

FString KzFountain::ToPascalCase(const FString& Text)
{
	FString Out;
	bool bWordStart = true;
	for (const TCHAR C : Text)
	{
		if (C == TEXT('\'') || C == 0x2019) { continue; }
		if (!FChar::IsAlnum(C))
		{
			bWordStart = true;
			continue;
		}
		Out += bWordStart ? FChar::ToUpper(C) : FChar::ToLower(C);
		bWordStart = false;
	}
	return Out;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKzFountainParseTest, "KzDialogue.Script.FountainParse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKzFountainParseTest::RunTest(const FString& Parameters)
{
	const FString Script =
		TEXT("Title: Test\nAuthor: Me\n\n")
		TEXT("KIRZO\nBefore any section.\n\n")
		TEXT("# CUTSCENE: THE KING'S END [[asset: DLG_End]]\n\n")
		TEXT("##EXT. FIELD - DAY\n\nAction /* cut */ here.\n\n")
		TEXT("MARA (CONT.)\n(dark)\nHello, *{Kirzo}*. [[Pause before.]]\nSecond (beat) line\x2014(sighs)\x2014") TEXT("done (dryly).\n")
		TEXT("KIRZO\n(scared)\nYes.\n[Insert: close-up.]\n\n")
		TEXT("GUARDS\n(laughing)\n")
		TEXT("#EMPTY\n\nJust action.\n");

	const TArray<FKzFountainSection> Sections = KzFountain::Parse(Script, TEXT("Default"));
	if (!TestEqual(TEXT("Sections"), Sections.Num(), 2)) { return false; }
	TestEqual(TEXT("Default title"), Sections[0].Title, TEXT("Default"));
	TestEqual(TEXT("Title"), Sections[1].Title, TEXT("CUTSCENE: THE KING'S END"));
	TestEqual(TEXT("Asset"), Sections[1].AssetName, TEXT("DLG_End"));
	if (!TestEqual(TEXT("Blocks"), Sections[1].Blocks.Num(), 3)) { return false; }

	const FKzFountainBlock& Mara = Sections[1].Blocks[0];
	TestEqual(TEXT("Cue"), Mara.Cue, TEXT("MARA"));
	TestEqual(TEXT("Text"), Mara.Text, FString(TEXT("Hello, {Kirzo}. Second line\x2014") TEXT("done.")));
	TestEqual(TEXT("Notes"), Mara.Notes, TEXT("dark; Pause before.; beat; sighs; dryly"));

	const FKzFountainBlock& Glued = Sections[1].Blocks[1];
	TestEqual(TEXT("Glued cue"), Glued.Cue, TEXT("KIRZO"));
	TestEqual(TEXT("Glued text"), Glued.Text, TEXT("Yes."));
	TestEqual(TEXT("Insert note"), Glued.Notes, TEXT("scared; Insert: close-up."));
	TestTrue(TEXT("Directions only"), Sections[1].Blocks[2].Text.IsEmpty() && Sections[1].Blocks[2].Notes == TEXT("laughing"));

	TestEqual(TEXT("PascalCase"), KzFountain::ToPascalCase(TEXT("MEETING - THE KING'S ROAD")), FString(TEXT("MeetingTheKingsRoad")));
	TestEqual(TEXT("Normalize"), KzFountain::NormalizeName(TEXT("Old Guard")), KzFountain::NormalizeName(TEXT("OLDGUARD")));
	return true;
}

#endif