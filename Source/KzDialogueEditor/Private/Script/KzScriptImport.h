// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

namespace KzScriptImport
{
	/** Imports a Fountain script into Folder: one dialogue per section, created or updated in place, after asking about unknown characters and confirming a preview. */
	void Import(const FString& ScriptFile, const FString& Folder);

	/** Content browser entries: Import Fountain Script on a folder, Reimport Script on dialogues. */
	void RegisterMenus();
}
