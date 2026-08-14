// Copyright 2026 kirzo

#include "KzDialogueAsset.h"
#include "KzDialogueTimeline.h"
#include "KzSpeakerAsset.h"
#include "Sound/SoundBase.h"
#include "Misc/Crc.h"
#include "Internationalization/Text.h"
#include "UObject/AssetRegistryTagsContext.h"

UE_DISABLE_OPTIMIZATION

#if WITH_EDITOR
namespace
{
	uint32 ComputeSourceHash(const FText& Text)
	{
		// GetSourceString returns the authored string,
		// ignoring the active culture's translation.
		// Text.ToString() would hash the translation,
		// which defeats the whole point of drift detection.
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? FCrc::StrCrc32(**Source) : 0;
	}

	// Keeps the custom playback range coherent on interactive edits: inside the wave, and
	// AudioStartTime strictly below AudioEndTime whenever the end is set (both 0 = no range).
	// bEndEdited picks which side moves when they collide.
	void ClampAudioRange(FKzDialogueLine& Line, bool bEndEdited)
	{
		if (Line.Audio.IsNull()) { return; }

		float WaveDuration = 0.0f;
		if (const USoundBase* Sound = Line.Audio.LoadSynchronous())
		{
			WaveDuration = Sound->GetDuration();
		}

		constexpr float MinRange = 0.05f;
		Line.AudioStartTime = FMath::Max(0.0f, Line.AudioStartTime);
		Line.AudioEndTime = FMath::Max(0.0f, Line.AudioEndTime);
		if (WaveDuration > 0.0f)
		{
			Line.AudioEndTime = Line.AudioEndTime > 0.0f ? FMath::Min(Line.AudioEndTime, WaveDuration) : 0.0f;
			Line.AudioStartTime = FMath::Min(Line.AudioStartTime, FMath::Max(0.0f, WaveDuration - MinRange));
		}

		if (Line.AudioEndTime > 0.0f && Line.AudioStartTime >= Line.AudioEndTime)
		{
			if (bEndEdited)
			{
				Line.AudioEndTime = Line.AudioStartTime + MinRange;
				if (WaveDuration > 0.0f && Line.AudioEndTime > WaveDuration)
				{
					Line.AudioEndTime = WaveDuration;
					Line.AudioStartTime = FMath::Max(0.0f, WaveDuration - MinRange);
				}
			}
			else
			{
				Line.AudioStartTime = FMath::Max(0.0f, Line.AudioEndTime - MinRange);
			}
		}
	}
}
#endif

UKzDialogueAsset::UKzDialogueAsset()
{
}

int32 UKzDialogueAsset::IndexOfLine(const FGuid& LineId) const
{
	return Lines.IndexOfByPredicate([&LineId](const FKzDialogueLine& L) { return L.LineId == LineId; });
}

UKzDialogueTimeline* UKzDialogueAsset::FindTimelineForLine(const FGuid& LineId) const
{
	if (!LineId.IsValid()) { return nullptr; }
	for (const TObjectPtr<UKzDialogueTimeline>& Timeline : Timelines)
	{
		if (Timeline && Timeline->OwningLineId == LineId)
		{
			return Timeline;
		}
	}
	return nullptr;
}

void UKzDialogueAsset::FinalizeResolvedLine(FKzDialogueLine& OutLine) const
{
	// Single place every resolve path goes through, so consumers get a complete line: the transient
	// per-line timeline, and the asset-wide tags merged into the line's own (AppendTags dedupes, so
	// re-resolving the same line stays idempotent).
	OutLine.Timeline = FindTimelineForLine(OutLine.LineId);
	OutLine.Tags.AppendTags(Tags);
}

bool UKzDialogueAsset::TryGetLineById(const FGuid& InLineId, FKzDialogueLine& OutLine) const
{
	for (const FKzDialogueLine& Line : Lines)
	{
		if (Line.LineId == InLineId)
		{
			OutLine = Line;
			// Inject the asset's runtime data (timeline + asset-wide tags) so every asset-line lookup
			// carries it, not just the iterating provider (single-line and Sequencer paths go through here).
			FinalizeResolvedLine(OutLine);
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryGetAliasByName(FName AliasName, FKzDialogueAlias& OutAlias) const
{
	if (AliasName.IsNone()) { return false; }
	for (const FKzDialogueAlias& Alias : Aliases)
	{
		if (Alias.AliasName == AliasName)
		{
			OutAlias = Alias;
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryGetAliasById(const FGuid& InAliasId, FKzDialogueAlias& OutAlias) const
{
	if (!InAliasId.IsValid()) { return false; }
	for (const FKzDialogueAlias& Alias : Aliases)
	{
		if (Alias.AliasId == InAliasId)
		{
			OutAlias = Alias;
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryResolveAlias(const FGuid& InAliasId, FKzDialogueLine& OutLine) const
{
	FKzDialogueAlias Alias;
	if (!TryGetAliasById(InAliasId, Alias)) { return false; }

	// Filter to LineIds that still exist in the asset (to gracefully ignore stale ids).
	TArray<int32> ValidIndices;
	ValidIndices.Reserve(Alias.LineIds.Num());
	for (int32 i = 0; i < Alias.LineIds.Num(); ++i)
	{
		for (const FKzDialogueLine& Line : Lines)
		{
			if (Line.LineId == Alias.LineIds[i])
			{
				ValidIndices.Add(i);
				break;
			}
		}
	}

	if (ValidIndices.Num() == 0) { return false; }

	const int32 PickIdx = FMath::RandRange(0, ValidIndices.Num() - 1);
	const FGuid PickedId = Alias.LineIds[ValidIndices[PickIdx]];
	return TryGetLineById(PickedId, OutLine);
}

bool UKzDialogueAsset::TryResolveAlias(FName AliasName, FKzDialogueLine& OutLine) const
{
	FKzDialogueAlias Alias;
	if (!TryGetAliasByName(AliasName, Alias)) { return false; }
	return TryResolveAlias(Alias.AliasId, OutLine);
}

bool UKzDialogueAsset::TryResolveLineOrAlias(const FGuid& Id, FKzDialogueLine& OutLine) const
{
	if (!Id.IsValid()) { return false; }

	// Try lines first (cheaper than alias resolution which loops twice).
	if (TryGetLineById(Id, OutLine)) { return true; }

	// Fall back to alias resolution. RefreshLineMetadata guarantees Lines and Aliases
	// don't share GUIDs, so a hit here is unambiguous.
	return TryResolveAlias(Id, OutLine);
}

bool UKzDialogueAsset::TryResolveReference(const FKzDialogueAssetReference& Reference, FKzDialogueLine& OutLine) const
{
	if (!Reference.IsValid()) { return false; }
	return Reference.bIsAlias
		? TryResolveAlias(Reference.Id, OutLine)
		: TryGetLineById(Reference.Id, OutLine);
}

FPrimaryAssetId UKzDialogueAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("KzDialogue"), GetFName());
}

#if WITH_EDITOR

const FName UKzDialogueAsset::TagLineCount(TEXT("KzLineCount"));
const FName UKzDialogueAsset::TagVoicedCount(TEXT("KzVoicedCount"));
const FName UKzDialogueAsset::TagSpeakers(TEXT("KzSpeakers"));
const FName UKzDialogueAsset::TagDisplayName(TEXT("KzDisplayName"));

void UKzDialogueAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	// Baked at save time so the dialogue dashboard lists and filters without loading assets.
	int32 Voiced = 0;
	TSet<FString> SpeakerNames;
	for (const FKzDialogueLine& Line : Lines)
	{
		if (!Line.Audio.IsNull()) { ++Voiced; }
		if (Line.Speaker.Asset) { SpeakerNames.Add(Line.Speaker.Asset->GetName()); }
	}
	TArray<FString> SortedSpeakers = SpeakerNames.Array();
	SortedSpeakers.Sort();

	Context.AddTag(FAssetRegistryTag(TagLineCount, LexToString(Lines.Num()), FAssetRegistryTag::TT_Numerical));
	Context.AddTag(FAssetRegistryTag(TagVoicedCount, LexToString(Voiced), FAssetRegistryTag::TT_Numerical));
	Context.AddTag(FAssetRegistryTag(TagSpeakers, FString::Join(SortedSpeakers, TEXT(";")), FAssetRegistryTag::TT_Alphabetical));
	Context.AddTag(FAssetRegistryTag(TagDisplayName, DisplayName, FAssetRegistryTag::TT_Alphabetical));
}

void UKzDialogueAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshLineMetadata();
}

void UKzDialogueAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	const bool bAudioChanged = ChangedProperty == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, Audio);
	const bool bRangeChanged = ChangedProperty == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioStartTime) || ChangedProperty == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime);
	if (bAudioChanged || bRangeChanged)
	{
		const int32 LineIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, Lines).ToString());
		if (Lines.IsValidIndex(LineIndex))
		{
			// Hash stamping and range resets live in RefreshLineMetadata (path-tracked), which
			// unlike this chain hook also covers external-structure detail rows. Here only the
			// polite clamp remains, moving the side the user actually edited.
			ClampAudioRange(Lines[LineIndex], /*bEndEdited=*/ChangedProperty == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime));
		}
	}

	RefreshLineMetadata();
}

void UKzDialogueAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (bDuplicateForPIE) return;

	AssetId = FGuid::NewGuid();

	// Force regeneration after duplication so duplicated lines don't share GUIDs
	// with the originals (which would break Sequencer references in copies).
	for (FKzDialogueLine& Line : Lines)
	{
		Line.LineId = FGuid::NewGuid();
	}

	for (FKzDialogueAlias& Alias : Aliases)
	{
		Alias.AliasId = FGuid::NewGuid();
	}

	// Re-anchor FText keys to the freshly generated GUIDs.
	RefreshLineMetadata();
}

void UKzDialogueAsset::PostLoad()
{
	Super::PostLoad();

	// Migration: assets saved before AssetId existed get one now.
	if (!AssetId.IsValid())
	{
		AssetId = FGuid::NewGuid();
		MarkPackageDirty();
	}

	RefreshLineMetadata();
}

void UKzDialogueAsset::PostInitProperties()
{
	Super::PostInitProperties();

	// Only generate for freshly-created instances, not for the CDO or for objects
	// currently being loaded (PostLoad handles the migration path).
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad))
	{
		if (!AssetId.IsValid())
		{
			AssetId = FGuid::NewGuid();
		}
	}
}

void UKzDialogueAsset::RefreshLineMetadata()
{
	bool bDirty = false;

	// AssetId safety net (PostInitProperties/PostLoad usually cover this).
	if (!AssetId.IsValid())
	{
		AssetId = FGuid::NewGuid();
		bDirty = true;
	}

	// Lines
	{
		TSet<FGuid> Seen;
		Seen.Reserve(Lines.Num());
		for (FKzDialogueLine& Line : Lines)
		{
			if (!Line.LineId.IsValid() || Seen.Contains(Line.LineId))
			{
				Line.LineId = FGuid::NewGuid();
				bDirty = true;
			}
			Seen.Add(Line.LineId);
		}
	}

	// Aliases
	{
		TSet<FGuid> Seen;
		Seen.Reserve(Aliases.Num());
		for (FKzDialogueAlias& Alias : Aliases)
		{
			if (!Alias.AliasId.IsValid() || Seen.Contains(Alias.AliasId))
			{
				Alias.AliasId = FGuid::NewGuid();
				bDirty = true;
			}
			Seen.Add(Alias.AliasId);
		}
	}

	// Prune timelines whose owning line no longer exists.
	{
		const int32 Removed = Timelines.RemoveAll([this](const TObjectPtr<UKzDialogueTimeline>& Timeline)
		{
			return !Timeline || IndexOfLine(Timeline->OwningLineId) == INDEX_NONE;
		});
		if (Removed > 0) { bDirty = true; }
	}
	
	// Re-anchor every FText to its stable (Namespace, Key).
	RebindFTextKeys();

	// Refresh source text hashes. Compared on import to flag stale translations
	// for review when the authored text drifts.
	for (FKzDialogueLine& Line : Lines)
	{
		const uint32 NewTextHash = ComputeSourceHash(Line.Text);
		if (NewTextHash != Line.SourceTextHash)
		{
			Line.SourceTextHash = NewTextHash;
			bDirty = true;
		}

		// Keep RecordedTextHash coherent with the audio slot. Tracking the take's PATH makes
		// re-record detection work through every edit path (the external-structure details
		// rows never reach the property-chain hook): a different take re-baselines the hash
		// to the current text; the same take keeps its stale state until explicitly accepted.
		if (Line.Audio.IsNull())
		{
			if (Line.RecordedTextHash != 0 || Line.RecordedAudioPath.IsValid())
			{
				Line.RecordedTextHash = 0;
				Line.RecordedAudioPath.Reset();
				bDirty = true;
			}
		}
		else
		{
			const FSoftObjectPath CurrentAudioPath = Line.Audio.ToSoftObjectPath();
			if (Line.RecordedTextHash == 0)
			{
				Line.RecordedTextHash = NewTextHash;
				Line.RecordedAudioPath = CurrentAudioPath;
				bDirty = true;
			}
			else if (!Line.RecordedAudioPath.IsValid())
			{
				// Legacy data from before the path was tracked: adopt the current take
				// without touching the stale state.
				Line.RecordedAudioPath = CurrentAudioPath;
				bDirty = true;
			}
			else if (Line.RecordedAudioPath != CurrentAudioPath)
			{
				Line.RecordedTextHash = NewTextHash;
				Line.RecordedAudioPath = CurrentAudioPath;
				bDirty = true;
			}
		}

		// Playback-range invariant, as a net for edit paths that bypass the interactive clamp
		// (external-structure details rows do not carry the array chain): a set end always
		// stays above the start (0 keeps meaning "natural end"), and no audio = no range.
		if (Line.Audio.IsNull())
		{
			if (Line.AudioStartTime != 0.0f || Line.AudioEndTime != 0.0f)
			{
				Line.AudioStartTime = 0.0f;
				Line.AudioEndTime = 0.0f;
				bDirty = true;
			}
		}
		else if (Line.AudioEndTime > 0.0f && Line.AudioEndTime <= Line.AudioStartTime)
		{
			Line.AudioEndTime = Line.AudioStartTime + 0.05f;
			bDirty = true;
		}
	}

	if (bDirty)
	{
		MarkPackageDirty();
	}
}

void UKzDialogueAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return; }

	const FString Namespace = FString::Printf(TEXT("KzDialogue.%s"), *AssetId.ToString(EGuidFormats::Digits));

	for (FKzDialogueLine& Line : Lines)
	{
		if (!Line.LineId.IsValid()) { continue; }

		const FString LineGuid = Line.LineId.ToString(EGuidFormats::Digits);

		Line.Text = FText::ChangeKey(Namespace, LineGuid + TEXT("-Text"), Line.Text);
	}
}

#endif

UE_ENABLE_OPTIMIZATION