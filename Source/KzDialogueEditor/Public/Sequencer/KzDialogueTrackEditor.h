// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"
#include "KzDialogueTypes.h"

/**
 * Track editor for UMovieSceneKzDialogueTrack. Adds:
 *   - Add Track menu entry under "Tracks" in the Sequencer
 *   - Section creation: pick a line from the asset bound to the track
 *   - Section right-click menu: "Change line..." reusing the same picker
 *   - Outliner icon and native-style "+ Section" button
 */
class FKzDialogueTrackEditor : public FMovieSceneTrackEditor
{
public:
	explicit FKzDialogueTrackEditor(TSharedRef<ISequencer> InSequencer);

	/** Factory used by registration. */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);

	//~ FMovieSceneTrackEditor
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual void BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;

	/** Icon shown next to the track name in the Sequencer outliner. */
	virtual const FSlateBrush* GetIconBrush() const override;

private:
	void HandleAddTrackMenuEntry();
	void HandleAddSection(FKzDialogueAssetReference Reference, float DefaultDurationSeconds, class UMovieSceneKzDialogueTrack* Track);

	TSharedRef<SWidget> BuildAddSectionMenu(class UMovieSceneKzDialogueTrack* Track);
};

/** Slate face of a single Kz Dialogue section in the Sequencer timeline. */
class FKzDialogueSection : public ISequencerSection
{
public:
	FKzDialogueSection(class UMovieSceneKzDialogueSection& InSection, class UMovieSceneKzDialogueTrack& InTrack);
	//~ ISequencerSection
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual FText GetSectionTitle() const override;
	virtual float GetSectionHeight() const override { return 28.f; }
	virtual int32 OnPaintSection(FSequencerSectionPainter& InPainter) const override;
private:
	class UMovieSceneKzDialogueSection* Section = nullptr;
	class UMovieSceneKzDialogueTrack* Track = nullptr;
};