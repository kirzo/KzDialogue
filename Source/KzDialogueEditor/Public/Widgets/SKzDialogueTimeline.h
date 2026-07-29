// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/AudioComponent.h"

class UKzDialogueTimeline;
class USoundWave;
class USoundBase;
class UAudioComponent;
class FScopedTransaction;
class IStructureDetailsView;
class FStructOnScope;
struct FKzDialogueNotifyEvent;
struct FPropertyChangedEvent;
struct FKzWaveformPreview;

/**
 * Visual timeline editor for a UKzDialogueTimeline, copied from the AnimMontage notify
 * panel (SAnimTimeline): a resizable splitter with a left outliner (a filter box, the
 * "Notifies" group whose "+ Track" dropdown adds notify tracks, and one row per track) and a
 * right track area (the engine time ruler on top, then one painted track per row). Markers
 * are point/state notifies placed along the line duration; drag one to retime it (drag a
 * state's edges to resize), right-click empty space to add a notify, right-click a marker to
 * delete it. An inline editor for the selected event sits below.
 */
class SKzDialogueTimeline : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueTimeline) {}
		/** Line duration in seconds, used to place events along the ruler. */
		SLATE_ATTRIBUTE(float, DisplayDuration)
		/** Fired after any edit so the host can mark the asset dirty. */
		SLATE_EVENT(FSimpleDelegate, OnModified)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UKzDialogueTimeline* InTimeline);
	virtual ~SKzDialogueTimeline();

	float Duration() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Ask the open timeline for the given line to select a specific notify (e.g. from a validation
	 *  issue click). The matching widget consumes the request on its next tick. */
	static void RequestNotifySelection(const FGuid& OwningLineId, int32 TrackIndex, int32 EventIndex);

private:
	void Rebuild();

	TSharedRef<SWidget> BuildRuler();
	TSharedRef<SWidget> BuildNotifiesHeaderRow(float RowHeightOverride);
	TSharedRef<SWidget> BuildOutlinerTrackRow(int32 TrackIndex);
	TSharedRef<SWidget> BuildTrackAreaRow(int32 TrackIndex);
	TSharedRef<SWidget> MakeAddMenuForTrack(int32 TrackIndex, float TimeSeconds);
	TSharedRef<SWidget> MakeNotifyClassPicker(UClass* BaseClass, int32 TrackIndex, float TimeSeconds);
	void CreateNewNotify(int32 TrackIndex, UClass* ParentClass, float TimeSeconds);
	TSharedRef<SWidget> MakeEventMenuForTrack(int32 TrackIndex, int32 EventIndex);

	void AddTrack();
	void RemoveTrack(int32 TrackIndex);
	void RenameTrack(int32 TrackIndex, const FText& NewName);
	FName GetNewTrackName() const;
	bool PassesFilter(FName TrackName) const;

	void AddEventAt(int32 TrackIndex, UClass* NotifyClass, float TimeSeconds);
	void RemoveSelected();
	void RemoveEvent(int32 TrackIndex, int32 EventIndex);
	void ToggleEventEnabled(int32 TrackIndex, int32 EventIndex);
	void DuplicateEvent(int32 TrackIndex, int32 EventIndex);
	void CopyEvent(int32 TrackIndex, int32 EventIndex);
	void PasteEvent(int32 TrackIndex, float TimeSeconds);
	void SetSelection(int32 TrackIndex, int32 EventIndex);
	void SyncEventDetails();
	void OnEventDetailsChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Dirties this widget and every ancestor so the hosting details panel re-measures us. */
	void InvalidateHostLayout();

	void BeginRetime();
	void RetimeEvent(int32 TrackIndex, int32 EventIndex, float StartSeconds, float EndSeconds);
	void EndRetime();
	void MoveEventToTrack(int32 FromTrack, int32 EventIndex, int32 ToTrack);
	void OpenEventNotifyAsset(int32 TrackIndex, int32 EventIndex);

	FKzDialogueNotifyEvent* SelectedEvent() const;

	/** Visible time window [start, end] in seconds, for zoom/pan; defaults to the whole line. */
	float GetViewStart() const;
	float GetViewEnd() const;
	void ZoomView(float CursorTimeSeconds, float WheelDelta, bool bPan);

	/** Selected event's start/end in seconds for the marker lines (end is -1 unless a state). */
	float GetSelectionStart() const;
	float GetSelectionEnd() const;
	void PanView(float DeltaSeconds);

	void Modified();

	/** Resolve + cache the line's audio waveform envelope for the group band; null if no drawable wave. */
	TSharedPtr<FKzWaveformPreview> GetWaveformPreview();

	/** The line's audio (USoundBase) for the editor preview; null if none. */
	USoundBase* ResolveLineAudio() const;

	/** Transport bar (To Front / Play-Pause / To End / Loop) driving the editor audio preview. */
	TSharedRef<SWidget> BuildTransportControl();

	void SetPlayhead(float Seconds);
	void ScrubTo(float Seconds);
	void StartPlayback();
	void StopPlayback();
	void TogglePlayback();
	float GetPlayheadTime() const { return PlayheadTime; }

	TWeakObjectPtr<UKzDialogueTimeline> Timeline;
	TAttribute<float> DisplayDuration;
	FSimpleDelegate OnModified;

	int32 SelTrack = INDEX_NONE;
	int32 SelEvent = INDEX_NONE;

	bool bNotifiesExpanded = true;
	FText FilterText;

	/** Visible window in seconds. ViewEnd <= ViewStart means "show the whole line". */
	float ViewStart = 0.f;
	float ViewEnd = 0.f;

	/** Splitter fill weights for the outliner / track-area columns, kept across rebuilds. */
	float ColumnFillCoefficients[2] = { 0.2f, 0.8f };

	/** Open while a marker is being dragged so the whole retime is one undo transaction. */
	TUniquePtr<FScopedTransaction> DragTransaction;

	/** Details of the selected event: its time source plus the notify with its C++/BP params.
	 * Edits a copy held in EventScope and is written back to the live event on change. */
	TSharedPtr<IStructureDetailsView> EventDetailsView;
	TSharedPtr<FStructOnScope> EventScope;
	int32 ScopeTrack = INDEX_NONE;
	int32 ScopeEvent = INDEX_NONE;

	/** Cached audio waveform envelope drawn in the group band; rebuilt when the line's wave changes. */
	TSharedPtr<FKzWaveformPreview> WaveformPreview;

	/** Frames left to keep re-dirtying the ancestor chain after a size-changing edit; the inner details view rebuilds deferred, so one frame is not enough. */
	int32 HostInvalidationFramesPending = 0;

	/** Editor-only audio preview: playhead position (seconds), transport state, and the live component. */
	float PlayheadTime = 0.f;
	bool bPlaying = false;
	bool bLooping = true;
	TStrongObjectPtr<UAudioComponent> PreviewAudio;

	static constexpr float RowHeight = 28.f;
	static constexpr float HeaderRowHeight = 24.f;
	static constexpr float WaveformBandHeight = 48.f;
};