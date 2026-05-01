// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSection.h"
#include "Misc/Guid.h"
#include "MovieSceneKzDialogueSection.generated.h"

/**
 * A Sequencer section that, while the playhead is inside it, plays a specific line of
 * the dialogue asset referenced by the parent track. The line is identified by its
 * stable LineId (FGuid) so that asset edits don't silently break references.
 *
 * Evaluation strategy: on entering the section, push the line manually through the
 * dialogue subsystem on the track's configured channel. The line's own duration logic
 * is suppressed: the section's duration governs how long the subtitle stays.
 */
UCLASS(MinimalAPI)
class UMovieSceneKzDialogueSection : public UMovieSceneSection
{
  GENERATED_BODY()

public:
  UMovieSceneKzDialogueSection(const FObjectInitializer& ObjectInitializer);

  /** Stable id of the line in the parent track's asset. */
  UPROPERTY(EditAnywhere, Category = "Dialogue")
  FGuid LineId;

  /** Cached display preview, kept up to date by the editor for nicer track UI. */
  UPROPERTY(VisibleAnywhere, Category = "Dialogue")
  FString DisplayName;

  //~ UMovieSceneSection
  virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
};