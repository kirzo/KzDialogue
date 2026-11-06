// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

/** One dialogue block: a character cue with its spoken text. Text is empty for a block that only has directions. */
struct FKzFountainBlock
{
	/** Character name as written, without extensions ("(CONT.)", "(V.O.)") or the '@' and '^' marks. */
	FString Cue;
	FString Text;

	/** Parentheticals and [[notes]], joined with "; ". */
	FString Notes;
};

/** A '#' section of the script holding dialogue: it becomes one dialogue asset. */
struct FKzFountainSection
{
	FString Title;

	/** Asset name pinned by an [[asset: Name]] note in the section, empty otherwise. */
	FString AssetName;

	TArray<FKzFountainBlock> Blocks;
};

namespace KzFountain
{
	/** Dialogue sections of a Fountain script. Dialogue before the first section goes to one titled DefaultTitle. Sections without dialogue are dropped. */
	TArray<FKzFountainSection> Parse(const FString& Script, const FString& DefaultTitle);

	/** A cue or token reduced for matching: uppercase letters and digits only ("Crow Enemy" and "CROWENEMY" match). */
	FString NormalizeName(const FString& Name);

	/** "LORE CHOICE - THE ORDER'S TRIBULATIONS" -> "LoreChoiceTheOrdersTribulations". */
	FString ToPascalCase(const FString& Text);
}
