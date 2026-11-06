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

	/** Line without its [[notes]], which go to OutNotes. */
	FString ExtractNotes(const FString& Line, TArray<FString>& OutNotes)
	{
		FString Out;
		int32 Pos = 0;
		while (Pos < Line.Len())
		{
			const int32 Open = Line.Find(TEXT("[["), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Line.Find(TEXT("]]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 2);
			if (Close == INDEX_NONE)
			{
				Out += Line.Mid(Pos);
				break;
			}
			Out += Line.Mid(Pos, Open - Pos);
			OutNotes.Add(Line.Mid(Open + 2, Close - Open - 2).TrimStartAndEnd());
			Pos = Close + 2;
		}
		return Out.Replace(TEXT("  "), TEXT(" ")).TrimStartAndEnd();
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

	/** A character cue: forced with '@', or an uppercase name (extensions aside) that is no scene heading or transition. */
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
			if (FCString::Strchr(TEXT("!>.=~["), Name[0]) || IsSceneHeading(Name) || Name.EndsWith(TEXT("TO:"))) { return false; }

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
		const FString Content = ExtractNotes(Lines[Index].TrimStartAndEnd(), Notes);

		if (bInBlock)
		{
			NoteParts.Append(Notes);
			if (Content.StartsWith(TEXT("(")) && Content.EndsWith(TEXT(")")))
			{
				NoteParts.Add(Content.Mid(1, Content.Len() - 2).TrimStartAndEnd());
			}
			else if (!Content.IsEmpty())
			{
				TextParts.Add(StripEmphasis(Content.StartsWith(TEXT("~")) ? Content.RightChop(1).TrimStart() : Content));
			}
			continue;
		}

		if (Content.StartsWith(TEXT("#")))
		{
			int32 Depth = 0;
			while (Depth < Content.Len() && Content[Depth] == TEXT('#')) { ++Depth; }
			Sections.AddDefaulted_GetRef().Title = Content.RightChop(Depth).TrimStartAndEnd();
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

		FString Cue;
		if (IsBlank(Index - 1) && !IsBlank(Index + 1) && !Content.StartsWith(TEXT("#")) && ParseCue(Content, Cue))
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
		TEXT("BOGDAN\nBefore any section.\n\n")
		TEXT("# CUTSCENE: THE ORDER'S END [[asset: DLG_End]]\n\n")
		TEXT("EXT. FIELD - DAY\n\nAction /* cut */ here.\n\n")
		TEXT("DUNCAN (CONT.)\n(dark)\nHello, *{Bogdan}*. [[Pause before.]]\nSecond line.\n\n")
		TEXT("CROW ENEMIES\n(laughing)\n\n")
		TEXT("CUT TO:\n\n")
		TEXT("# EMPTY\n\nJust action.\n");

	const TArray<FKzFountainSection> Sections = KzFountain::Parse(Script, TEXT("Default"));
	if (!TestEqual(TEXT("Sections"), Sections.Num(), 2)) { return false; }
	TestEqual(TEXT("Default title"), Sections[0].Title, TEXT("Default"));
	TestEqual(TEXT("Title"), Sections[1].Title, TEXT("CUTSCENE: THE ORDER'S END"));
	TestEqual(TEXT("Asset"), Sections[1].AssetName, TEXT("DLG_End"));
	if (!TestEqual(TEXT("Blocks"), Sections[1].Blocks.Num(), 2)) { return false; }

	const FKzFountainBlock& Duncan = Sections[1].Blocks[0];
	TestEqual(TEXT("Cue"), Duncan.Cue, TEXT("DUNCAN"));
	TestEqual(TEXT("Text"), Duncan.Text, TEXT("Hello, {Bogdan}. Second line."));
	TestEqual(TEXT("Notes"), Duncan.Notes, TEXT("dark; Pause before."));
	TestTrue(TEXT("Directions only"), Sections[1].Blocks[1].Text.IsEmpty() && Sections[1].Blocks[1].Notes == TEXT("laughing"));

	TestEqual(TEXT("PascalCase"), KzFountain::ToPascalCase(TEXT("LORE CHOICE - THE ORDER'S TRIBULATIONS")), FString(TEXT("LoreChoiceTheOrdersTribulations")));
	TestEqual(TEXT("Normalize"), KzFountain::NormalizeName(TEXT("Crow Enemy")), KzFountain::NormalizeName(TEXT("CROWENEMY")));
	return true;
}

#endif
