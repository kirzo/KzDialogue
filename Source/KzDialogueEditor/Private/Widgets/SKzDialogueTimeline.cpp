// Copyright 2026 kirzo

#include "Widgets/SKzDialogueTimeline.h"
#include "KzDialogueAudition.h"
#include "Widgets/KzWaveformPreview.h"
#include "Widgets/SKzTimelineTrack.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"
#include "KzDialogueAsset.h"

#include "Sound/SoundWave.h"

#include "UObject/UObjectHash.h"

#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorWidgetsModule.h"
#include "ITransportControl.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Styling/SlateTypes.h"

#include "PropertyEditorModule.h"
#include "IStructureDetailsView.h"
#include "UObject/StructOnScope.h"

#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SKzDialogueTimeline"

namespace
{
	// Class-viewer filter for the add menu: concrete subclasses of a notify base. Handles both
	// loaded classes and unloaded blueprints (the class viewer discovers them).
	class FKzNotifyClassFilter : public IClassViewerFilter
	{
	public:
		UClass* BaseClass = nullptr;

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return BaseClass && InClass->IsChildOf(BaseClass) && !InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return BaseClass && InUnloadedClassData->IsChildOf(BaseClass) && !InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		}
	};

	// Editor clipboard for notify events, shared across timeline widgets so copy/paste works between
	// lines. The notify is rooted; weak, since a notify Blueprint recompile unroots and trashes it.
	FInstancedStruct GClipboardTimeSource;
	bool GClipboardFireIfSkipped = false;
	bool GClipboardEnabled = true;
	TWeakObjectPtr<UKzDialogueNotifyBase> GClipboardNotify;

	// Pending notify selection (e.g. from a validation-issue click); consumed by the timeline whose
	// OwningLineId matches, on its next tick. LineId invalid means "nothing pending".
	FGuid GPendingSelectionLineId;
	int32 GPendingSelectionTrack = INDEX_NONE;
	int32 GPendingSelectionEvent = INDEX_NONE;
}

// ---------------------------------------------------------------------------------------
// SKzDialogueTimeline
// ---------------------------------------------------------------------------------------

void SKzDialogueTimeline::Construct(const FArguments& InArgs, UKzDialogueTimeline* InTimeline)
{
	Timeline = InTimeline;
	DisplayDuration = InArgs._DisplayDuration;
	RefreshDuration();
	OnModified = InArgs._OnModified;
	OnRequestHostRefresh = InArgs._OnRequestHostRefresh;
	PreviewAudioKey = FString::Printf(TEXT("Timeline_%p"), this);
	FEditorDelegates::BeginPIE.AddSPLambda(this, [this](bool) { StopPlayback(); });

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	StructArgs.bShowObjects = true;
	EventDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, nullptr, FText::GetEmpty());
	EventDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SKzDialogueTimeline::OnEventDetailsChanged);

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	RefreshWaveformPreview();

	// Two synchronized columns, like the AnimMontage notify panel: the left outliner (filter,
	// transport, the "Notifies" group, one row per track) and the right track area (ruler,
	// waveform band, group band, one painted track per row). Matching row heights keep the
	// columns aligned. Built once: Rebuild only refills the track rows.
	auto WaveRowHeight = [this]() -> FOptionalSize { return WaveformPreview.IsValid() && WaveformPreview->Peaks.Num() > 0 ? WaveformBandHeight : HeaderRowHeight; };

	TSharedRef<SVerticalBox> OutlinerColumn = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(HeaderRowHeight).VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SSearchBox)
						.HintText(LOCTEXT("FilterTracksHint", "Filter tracks"))
						.OnTextChanged_Lambda([this](const FText& InText) { FilterText = InText; Rebuild(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
				[
					SNew(SButton)
						.ToolTipText(LOCTEXT("ResetZoomTip", "Reset zoom (show the whole line)"))
						.ContentPadding(FMargin(4.f, 1.f))
						.OnClicked_Lambda([this]() { ViewStart = 0.f; ViewEnd = 0.f; Invalidate(EInvalidateWidgetReason::Paint); return FReply::Handled(); })
						[
							SNew(STextBlock).Text(LOCTEXT("ResetZoom", "Fit")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
				]
			]
		]
		// Playback row, above the "Notifies" row so that row keeps its compact label height. The
		// time readout is clipped so it never spills into the track area when the column is narrow.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride_Lambda(WaveRowHeight).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f)).Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2f / %.2f s"), PlayheadTime, Duration())); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					BuildTransportControl()
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[ BuildNotifiesHeaderRow(HeaderRowHeight) ]
		+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(OutlinerTracks, SVerticalBox) ];

	TSharedRef<SVerticalBox> TrackAreaColumn = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(HeaderRowHeight)[ BuildRuler() ] ]
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride_Lambda(WaveRowHeight)[ MakeLane(INDEX_NONE, /*bWithWaveform=*/true) ] ]
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(HeaderRowHeight)[ MakeLane(INDEX_NONE) ] ]
		+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(TrackAreaTracks, SVerticalBox) ];

	ChildSlot
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSplitter)
					.Orientation(Orient_Horizontal)
				+ SSplitter::Slot().Value(0.2f)
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(0)[ OutlinerColumn ]
					]
				+ SSplitter::Slot().Value(0.8f)
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(0)[ TrackAreaColumn ]
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				EventDetailsView.IsValid() ? EventDetailsView->GetWidget().ToSharedRef() : SNullWidget::NullWidget
			]
		]
	];

	Rebuild();
}

SKzDialogueTimeline::~SKzDialogueTimeline()
{
	StopPlayback();
}

float SKzDialogueTimeline::Duration() const
{
	return CachedDuration;
}

void SKzDialogueTimeline::RefreshDuration()
{
	const float D = DisplayDuration.Get(0.f);
	CachedDuration = D > UE_KINDA_SMALL_NUMBER ? D : 1.f;
}

float SKzDialogueTimeline::GetViewStart() const
{
	if (ViewEnd <= ViewStart) { return 0.f; }
	return FMath::Clamp(ViewStart, 0.f, Duration());
}

float SKzDialogueTimeline::GetViewEnd() const
{
	const float Dur = Duration();
	if (ViewEnd <= ViewStart) { return Dur; }
	return FMath::Clamp(ViewEnd, GetViewStart() + 0.001f, Dur);
}

float SKzDialogueTimeline::GetSelectionStart() const
{
	const FKzDialogueNotifyEvent* Event = SelectedEvent();
	const FKzDialogueTimeSource* Source = Event ? Event->TimeSource.GetPtr<FKzDialogueTimeSource>() : nullptr;
	if (!Source) { return -1.f; }

	FKzDialogueTimeResolveContext Context;
	Context.LineDuration = Duration();
	Context.Audio = ResolveLineAudio();
	Context.AudioOffset = GetAudioTimeOffset();
	float Start = 0.f;
	float End = 0.f;
	Source->Resolve(Context, Start, End);
	return Start;
}

float SKzDialogueTimeline::GetSelectionEnd() const
{
	const FKzDialogueNotifyEvent* Event = SelectedEvent();
	if (!Event || !Event->Notify || !Event->Notify->IsA<UKzDialogueNotifyState>()) { return -1.f; }
	const FKzDialogueTimeSource* Source = Event->TimeSource.GetPtr<FKzDialogueTimeSource>();
	if (!Source) { return -1.f; }

	FKzDialogueTimeResolveContext Context;
	Context.LineDuration = Duration();
	Context.Audio = ResolveLineAudio();
	Context.AudioOffset = GetAudioTimeOffset();
	float Start = 0.f;
	float End = 0.f;
	Source->Resolve(Context, Start, End);
	return End;
}

void SKzDialogueTimeline::ZoomView(float CursorTimeSeconds, float WheelDelta, bool bPan)
{
	const float Dur = Duration();
	float Start = GetViewStart();
	float End = GetViewEnd();
	const float Size = End - Start;

	if (bPan)
	{
		const float Shift = Size * 0.15f * (WheelDelta > 0.f ? 1.f : -1.f);
		Start += Shift;
		End += Shift;
	}
	else
	{
		const float Factor = (WheelDelta > 0.f) ? 0.8f : 1.25f;
		const float NewSize = FMath::Clamp(Size * Factor, FMath::Min(0.05f, Dur), Dur);
		const float Frac = Size > UE_KINDA_SMALL_NUMBER ? (CursorTimeSeconds - Start) / Size : 0.5f;
		Start = CursorTimeSeconds - Frac * NewSize;
		End = Start + NewSize;
	}

	// Keep the window size and slide it back inside [0, Dur].
	const float WindowSize = FMath::Min(End - Start, Dur);
	if (Start < 0.f) { Start = 0.f; }
	End = Start + WindowSize;
	if (End > Dur) { End = Dur; Start = FMath::Max(0.f, End - WindowSize); }

	// Fully zoomed out is stored as the sentinel (End <= Start = whole line), not as concrete
	// [0, Dur]: a concrete window would stick to TODAY'S duration and miss it growing later
	// (e.g. clearing AudioEndTime), while the sentinel always resolves to the full range.
	if (WindowSize >= Dur - UE_KINDA_SMALL_NUMBER)
	{
		ViewStart = 0.f;
		ViewEnd = 0.f;
	}
	else
	{
		ViewStart = Start;
		ViewEnd = End;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKzDialogueTimeline::PanView(float DeltaSeconds)
{
	const float Dur = Duration();
	float Start = GetViewStart();
	const float Size = GetViewEnd() - Start;
	Start += DeltaSeconds;
	float End = Start + Size;
	if (Start < 0.f) { Start = 0.f; End = Start + Size; }
	if (End > Dur) { End = Dur; Start = FMath::Max(0.f, End - Size); }

	// Same sentinel rule as ZoomView: a full-range window must not freeze today's duration.
	if (Size >= Dur - UE_KINDA_SMALL_NUMBER)
	{
		ViewStart = 0.f;
		ViewEnd = 0.f;
	}
	else
	{
		ViewStart = Start;
		ViewEnd = End;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FKzDialogueNotifyEvent* SKzDialogueTimeline::FindEvent(int32 TrackIndex, int32 EventIndex) const
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return nullptr; }
	FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];
	return Track.Events.IsValidIndex(EventIndex) ? &Track.Events[EventIndex] : nullptr;
}

void SKzDialogueTimeline::EditTimeline(const FText& Title, TFunctionRef<void(UKzDialogueTimeline&)> Edit, bool bRebuild)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T) { return; }

	const FScopedTransaction Transaction(Title);
	T->Modify();
	Edit(*T);
	Modified();
	if (bRebuild)
	{
		Rebuild();
	}
}

void SKzDialogueTimeline::Modified()
{
	OnModified.ExecuteIfBound();
}

USoundBase* SKzDialogueTimeline::ResolveLineAudio() const
{
	// The line's audio lives on the owning asset, keyed by the timeline's line id.
	if (UKzDialogueTimeline* T = Timeline.Get())
	{
		if (const UKzDialogueAsset* Asset = T->GetTypedOuter<UKzDialogueAsset>())
		{
			if (const FKzDialogueLine* Line = Asset->FindLine(T->OwningLineId))
			{
				return Line->Audio.LoadSynchronous();
			}
		}
	}
	return nullptr;
}

float SKzDialogueTimeline::GetAudioTimeOffset() const
{
	if (UKzDialogueTimeline* T = Timeline.Get())
	{
		if (const UKzDialogueAsset* Asset = T->GetTypedOuter<UKzDialogueAsset>())
		{
			if (const FKzDialogueLine* Line = Asset->FindLine(T->OwningLineId))
			{
				return Line->GetAudioTimelineOffset();
			}
		}
	}
	return 0.0f;
}

bool SKzDialogueTimeline::RefreshWaveformPreview()
{
	// Only a direct USoundWave is drawable (a SoundCue / MetaSound wrapper has no single waveform).
	USoundWave* Wave = Cast<USoundWave>(ResolveLineAudio());
	if (!Wave)
	{
		const bool bHadPreview = WaveformPreview.IsValid();
		WaveformPreview.Reset();
		return bHadPreview;
	}

	// Decoding is not free: rebuild the envelope only when the wave changes.
	if (!WaveformPreview.IsValid() || WaveformPreview->Wave.Get() != Wave)
	{
		WaveformPreview = KzBuildWaveformPreview(Wave);
		return true;
	}
	return false;
}

void SKzDialogueTimeline::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RefreshDuration();

	// The hosting details panel's tree only re-measures on RequestTreeRefresh or when its own
	// geometry changes size (that is why an external resize used to "fix" the stale scrollbar).
	// Keep requesting the refresh for a couple of frames after a size-changing edit: the inner
	// structure details view rebuilds its content deferred, so the first frame still measures
	// the pre-growth size.
	if (HostInvalidationFramesPending > 0)
	{
		--HostInvalidationFramesPending;
		OnRequestHostRefresh.ExecuteIfBound();
	}

	// Consume a pending notify selection (e.g. from a validation-issue click) once the timeline for
	// the requested line is alive and ticking.
	if (GPendingSelectionLineId.IsValid())
	{
		UKzDialogueTimeline* PendingTimeline = Timeline.Get();
		if (PendingTimeline && PendingTimeline->OwningLineId == GPendingSelectionLineId)
		{
			const int32 PendingTrack = GPendingSelectionTrack;
			const int32 PendingEvent = GPendingSelectionEvent;
			GPendingSelectionLineId.Invalidate();
			bNotifiesExpanded = true;
			SetSelection(PendingTrack, PendingEvent);
			Rebuild();
		}
	}

	// The line's audio is edited elsewhere in the asset editor (set / swapped / cleared), so the
	// timeline polls it; the waveform band reads the cached envelope and its row height follows.
	if (RefreshWaveformPreview())
	{
		Invalidate(EInvalidateWidgetReason::Layout);
	}

	// Advance the preview playhead while playing.
	if (bPlaying)
	{
		const float Dur = Duration();
		PlayheadTime += InDeltaTime;
		if (PlayheadTime >= Dur)
		{
			if (bLooping)
			{
				PlayheadTime = 0.f;
				StartPlayback();
			}
			else
			{
				PlayheadTime = Dur;
				StopPlayback();
			}
		}
		else if (bPreviewAudioPending && PlayheadTime >= GetAudioTimeOffset())
		{
			PlayPreviewAudioFromPlayhead();
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SKzDialogueTimeline::SetPlayhead(float Seconds)
{
	PlayheadTime = FMath::Clamp(Seconds, 0.f, Duration());
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKzDialogueTimeline::ScrubTo(float Seconds)
{
	SetPlayhead(Seconds);

	// Keep keyboard focus on the timeline (not on a transport button) so Space toggles playback.
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);

	// Scrubbing while playing re-seeks the audio and keeps going.
	if (bPlaying)
	{
		PlayPreviewAudioFromPlayhead();
	}
}

void SKzDialogueTimeline::TogglePlayback()
{
	if (bPlaying)
	{
		StopPlayback();
	}
	else
	{
		if (PlayheadTime >= Duration() - UE_KINDA_SMALL_NUMBER) { PlayheadTime = 0.f; }
		StartPlayback();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKzDialogueTimeline::StartPlayback()
{
	StopPlayback();
	bPlaying = true;
	PlayPreviewAudioFromPlayhead();
}

void SKzDialogueTimeline::PlayPreviewAudioFromPlayhead()
{
	KzDialogueAudition::Stop(PreviewAudioKey);

	// Timeline time is line time, as the game plays it: silent through the AudioStartDelay (Tick starts the
	// audio when the playhead gets there), then the wave from its AudioStartTime. The range end falls out of
	// Duration(), where Tick stops or wraps.
	const float WaveTime = PlayheadTime - GetAudioTimeOffset();
	bPreviewAudioPending = WaveTime < 0.f;
	if (bPreviewAudioPending) { return; }

	KzDialogueAudition::Play(ResolveLineAudio(), PreviewAudioKey, WaveTime);
}

void SKzDialogueTimeline::StopPlayback()
{
	KzDialogueAudition::Stop(PreviewAudioKey);
	bPreviewAudioPending = false;
	bPlaying = false;
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildTransportControl()
{
	FEditorWidgetsModule& EditorWidgets = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");

	FTransportControlArgs Args;
	// Buttons are not focusable so Space is handled by the timeline (play/pause), not the focused button.
	Args.bAreButtonsFocusable = false;
	Args.OnBackwardEnd = FOnClicked::CreateLambda([this]()
	{
		SetPlayhead(0.f);
		if (bPlaying) { StartPlayback(); }
		return FReply::Handled();
	});
	Args.OnForwardPlay = FOnClicked::CreateLambda([this]() { TogglePlayback(); return FReply::Handled(); });
	Args.OnForwardEnd = FOnClicked::CreateLambda([this]()
	{
		SetPlayhead(Duration());
		// In loop mode while playing, stay playing and let Tick wrap back to the start naturally.
		if (!(bLooping && bPlaying)) { StopPlayback(); }
		return FReply::Handled();
	});
	Args.OnToggleLooping = FOnClicked::CreateLambda([this]() { bLooping = !bLooping; return FReply::Handled(); });
	Args.OnGetLooping = FOnGetLooping::CreateLambda([this]() { return bLooping; });
	Args.OnGetPlaybackMode = FOnGetPlaybackMode::CreateLambda([this]() { return bPlaying ? EPlaybackMode::PlayingForward : EPlaybackMode::Stopped; });
	Args.WidgetsToCreate =
	{
		FTransportControlWidget(ETransportControlWidgetType::BackwardEnd),
		FTransportControlWidget(ETransportControlWidgetType::ForwardPlay),
		// Custom Stop: stops playback and rewinds the playhead to 0 (the transport has no native stop).
		FTransportControlWidget(FOnMakeTransportWidget::CreateLambda([this]() -> TSharedRef<SWidget>
		{
			return SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Animation.PlayControlsButton")
				.ContentPadding(0.0f)
				.ToolTipText(LOCTEXT("StopTip", "Stop and rewind to the start"))
				.OnClicked_Lambda([this]() { StopPlayback(); SetPlayhead(0.f); return FReply::Handled(); })
				[
					SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Image(FAppStyle::Get().GetBrush("Animation.Stop"))
				];
		})),
		FTransportControlWidget(ETransportControlWidgetType::ForwardEnd),
		FTransportControlWidget(ETransportControlWidgetType::Loop)
	};

	return EditorWidgets.CreateTransportControl(Args);
}

FReply SKzDialogueTimeline::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	// A retime, or a drag aimed at another track (anchored markers only move).
	if (Key == EKeys::Escape && (DragTransaction.IsValid() || DragHoverTrack != INDEX_NONE))
	{
		CancelRetime();
		return FReply::Handled();
	}
	if (Key == EKeys::SpaceBar)
	{
		TogglePlayback();
		return FReply::Handled();
	}
	if (Key == EKeys::Delete && SelectedEvent())
	{
		RemoveSelected();
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown())
	{
		if (Key == EKeys::C && SelectedEvent())
		{
			CopyEvent(SelTrack, SelEvent);
			return FReply::Handled();
		}
		if (Key == EKeys::D && SelectedEvent())
		{
			DuplicateEvent(SelTrack, SelEvent);
			return FReply::Handled();
		}
		if (Key == EKeys::V && GClipboardNotify.IsValid())
		{
			// Onto the selected event's track, else the first one shown.
			const UKzDialogueTimeline* T = Timeline.Get();
			const int32 TargetTrack = (T && T->Tracks.IsValidIndex(SelTrack)) ? SelTrack : (VisibleTracks.Num() > 0 ? VisibleTracks[0] : INDEX_NONE);
			PasteEvent(TargetTrack, PlayheadTime);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SKzDialogueTimeline::Rebuild()
{
	OutlinerTracks->ClearChildren();
	TrackAreaTracks->ClearChildren();
	VisibleTracks.Reset();

	// One row per (filtered) track, when the group is expanded.
	UKzDialogueTimeline* T = Timeline.Get();
	if (T && bNotifiesExpanded)
	{
		for (int32 i = 0; i < T->Tracks.Num(); ++i)
		{
			if (!PassesFilter(T->Tracks[i].Name)) { continue; }
			VisibleTracks.Add(i);
			OutlinerTracks->AddSlot().AutoHeight()[ BuildOutlinerTrackRow(i) ];
			TrackAreaTracks->AddSlot().AutoHeight()[ BuildTrackAreaRow(i) ];
		}
	}

	SyncEventDetails();
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeLane(int32 TrackIndex, bool bWithWaveform)
{
	// Every lane shows the view, bounds, selection lines and playhead; only a real track takes edits.
	SKzTimelineTrack::FArguments Args;
	Args.Timeline(Timeline)
		.TrackIndex(TrackIndex)
		.Duration_Lambda([this]() { return Duration(); })
		.ViewStart_Lambda([this]() { return GetViewStart(); })
		.ViewEnd_Lambda([this]() { return GetViewEnd(); })
		.SelectionStart_Lambda([this]() { return GetSelectionStart(); })
		.SelectionEnd_Lambda([this]() { return GetSelectionEnd(); })
		.OnZoom_Lambda([this](float CursorSeconds, float WheelDelta, bool bPan) { ZoomView(CursorSeconds, WheelDelta, bPan); })
		.OnPan_Lambda([this](float DeltaSeconds) { PanView(DeltaSeconds); })
		.Playhead_Lambda([this]() { return GetPlayheadTime(); });

	if (bWithWaveform)
	{
		Args.Waveform_Lambda([this]() { return WaveformPreview; })
			.AudioTimeOffset_Lambda([this]() { return GetAudioTimeOffset(); })
			.LineAudio_Lambda([this]() { return ResolveLineAudio(); });
	}

	if (TrackIndex != INDEX_NONE)
	{
		Args.LineAudio_Lambda([this]() { return ResolveLineAudio(); })
			.AudioTimeOffset_Lambda([this]() { return GetAudioTimeOffset(); })
			.IsDropTarget_Lambda([this, TrackIndex]() { return DragHoverTrack == TrackIndex; })
			.OnDragRow_Lambda([this, TrackIndex](int32 RowDelta)
			{
				const int32 Target = TrackAtRowOffset(TrackIndex, RowDelta);
				const int32 NewHover = (Target != TrackIndex) ? Target : INDEX_NONE;
				if (NewHover != DragHoverTrack) { DragHoverTrack = NewHover; Invalidate(EInvalidateWidgetReason::Paint); }
			})
			.SelectedEvent_Lambda([this, TrackIndex]() { return (TrackIndex == SelTrack) ? SelEvent : INDEX_NONE; })
			.OnSelect_Lambda([this, TrackIndex](int32 EventIndex) { SetSelection(TrackIndex, EventIndex); })
			.OnBeginRetime_Lambda([this]() { BeginRetime(); })
			.OnRetime_Lambda([this, TrackIndex](int32 EventIndex, float StartSeconds, float EndSeconds) { RetimeEvent(TrackIndex, EventIndex, StartSeconds, EndSeconds); })
			.OnEndRetime_Lambda([this]() { EndRetime(); })
			.OnInteractionEnd_Lambda([this]() { Rebuild(); })
			.OnBuildAddMenu_Lambda([this, TrackIndex](float TimeSeconds) { return MakeAddMenuForTrack(TrackIndex, TimeSeconds); })
			.OnBuildEventMenu_Lambda([this, TrackIndex](int32 EventIndex) { return MakeEventMenuForTrack(TrackIndex, EventIndex); })
			.OnMoveToTrack_Lambda([this, TrackIndex](int32 EventIndex, int32 RowDelta) { MoveEventToTrack(TrackIndex, EventIndex, TrackAtRowOffset(TrackIndex, RowDelta)); })
			.OnActivateEvent_Lambda([this, TrackIndex](int32 EventIndex) { OpenEventNotifyAsset(TrackIndex, EventIndex); });
	}

	return SArgumentNew(Args, SKzTimelineTrack);
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildRuler()
{
	return SNew(SKzTimelineRuler)
		.ViewStart_Lambda([this]() { return GetViewStart(); })
		.ViewEnd_Lambda([this]() { return GetViewEnd(); })
		.Duration_Lambda([this]() { return Duration(); })
		.SelectionStart_Lambda([this]() { return GetSelectionStart(); })
		.SelectionEnd_Lambda([this]() { return GetSelectionEnd(); })
		.OnZoom_Lambda([this](float CursorSeconds, float WheelDelta, bool bPan) { ZoomView(CursorSeconds, WheelDelta, bPan); })
		.OnPan_Lambda([this](float DeltaSeconds) { PanView(DeltaSeconds); })
		.Playhead_Lambda([this]() { return GetPlayheadTime(); })
		.OnScrub_Lambda([this](float Seconds) { ScrubTo(Seconds); });
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildNotifiesHeaderRow(float RowHeightOverride)
{
	// The "Notifies" group: expander + label + the "+ Track" dropdown, styled like the
	// AnimMontage outliner header (FAnimTimelineTrack_Notifies::GenerateContainerWidgetForOutliner).
	return SNew(SBox).HeightOverride(RowHeightOverride)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Sequencer.Section.BackgroundTint"))
			.BorderBackgroundColor(FAppStyle::GetColor("AnimTimeline.Outliner.HeaderColor"))
			.VAlign(VAlign_Center)
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.OnClicked_Lambda([this]() { bNotifiesExpanded = !bNotifiesExpanded; Rebuild(); return FReply::Handled(); })
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image_Lambda([this]() { return FAppStyle::GetBrush(bNotifiesExpanded ? "TreeArrow_Expanded" : "TreeArrow_Collapsed"); })
						]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(STextBlock)
						.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimTimeline.Outliner.Label"))
						.Text(LOCTEXT("NotifiesRoot", "Notifies"))
				]
				+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4, 1)
				[
					PropertyCustomizationHelpers::MakeAddButton(FSimpleDelegate::CreateSP(this, &SKzDialogueTimeline::AddTrack), LOCTEXT("AddTrackTip", "Add a notify track"))
				]
			]
	];
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildOutlinerTrackRow(int32 TrackIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return SNullWidget::NullWidget; }
	const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];

	return SNew(SBox).HeightOverride(RowHeight)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Sequencer.Section.BackgroundTint"))
			.BorderBackgroundColor(FAppStyle::GetColor("AnimTimeline.Outliner.ItemColor"))
			.VAlign(VAlign_Center)
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SSpacer).Size(FVector2D(22.f, 1.f)) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(SEditableTextBox)
						.Text(FText::FromName(Track.Name))
						.HintText(LOCTEXT("TrackNameHint", "Track"))
						.OnTextCommitted_Lambda([this, TrackIndex](const FText& New, ETextCommit::Type) { RenameTrack(TrackIndex, New); })
				]
				+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4, 1)
				[
					PropertyCustomizationHelpers::MakeDeleteButton(FSimpleDelegate::CreateSP(this, &SKzDialogueTimeline::RemoveTrack, TrackIndex), LOCTEXT("DeleteTrackTip", "Delete this track and its notifies"), TAttribute<bool>::CreateLambda([this]() { UKzDialogueTimeline* T = Timeline.Get(); return T && T->Tracks.Num() > 1; }))
				]
			]
	];
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildTrackAreaRow(int32 TrackIndex)
{
	// No wrapping border here: the track paints its own background and must share the ruler's
	// exact left edge and width so markers line up with the ticks.
	return SNew(SBox).HeightOverride(RowHeight)[ MakeLane(TrackIndex) ];
}

void SKzDialogueTimeline::OpenEventNotifyAsset(int32 TrackIndex, int32 EventIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(EventIndex)) { return; }

	const UKzDialogueNotifyBase* Notify = T->Tracks[TrackIndex].Events[EventIndex].Notify;
	if (!IsValid(Notify)) { return; }

	// Only Blueprint notifies have an asset to open; native ones have nowhere to go.
	if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Notify->GetClass()))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
	}
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeAddMenuForTrack(int32 TrackIndex, float TimeSeconds)
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(NAME_None, LOCTEXT("AddSection", "Add"));
	{
		Menu.AddSubMenu(
			LOCTEXT("AddNotify", "Add Notify..."),
			LOCTEXT("AddNotifyTip", "Add a point notify at this time."),
			FNewMenuDelegate::CreateLambda([this, TrackIndex, TimeSeconds](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(MakeNotifyClassPicker(UKzDialogueNotify::StaticClass(), TrackIndex, TimeSeconds), FText::GetEmpty(), true);
			}));
		Menu.AddSubMenu(
			LOCTEXT("AddNotifyState", "Add Notify State..."),
			LOCTEXT("AddNotifyStateTip", "Add a ranged state notify at this time."),
			FNewMenuDelegate::CreateLambda([this, TrackIndex, TimeSeconds](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(MakeNotifyClassPicker(UKzDialogueNotifyState::StaticClass(), TrackIndex, TimeSeconds), FText::GetEmpty(), true);
			}));
	}
	Menu.EndSection();

	Menu.BeginSection(NAME_None, LOCTEXT("CreateSection", "Create"));
	{
		Menu.AddMenuEntry(
			LOCTEXT("NewNotify", "New Notify..."),
			LOCTEXT("NewNotifyTip", "Create a new notify Blueprint and add it here."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, TimeSeconds]() { CreateNewNotify(TrackIndex, UKzDialogueNotify::StaticClass(), TimeSeconds); })));
		Menu.AddMenuEntry(
			LOCTEXT("NewNotifyState", "New Notify State..."),
			LOCTEXT("NewNotifyStateTip", "Create a new notify state Blueprint and add it here."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, TimeSeconds]() { CreateNewNotify(TrackIndex, UKzDialogueNotifyState::StaticClass(), TimeSeconds); })));
	}
	Menu.EndSection();

	if (GClipboardNotify.IsValid())
	{
		Menu.BeginSection(NAME_None, LOCTEXT("ClipboardSection", "Clipboard"));
		{
			Menu.AddMenuEntry(
				LOCTEXT("PasteNotify", "Paste"),
				LOCTEXT("PasteNotifyTip", "Paste the copied notify at this time."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, TimeSeconds]() { PasteEvent(TrackIndex, TimeSeconds); })));
		}
		Menu.EndSection();
	}

	return Menu.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeNotifyClassPicker(UClass* BaseClass, int32 TrackIndex, float TimeSeconds)
{
	TSharedRef<FKzNotifyClassFilter> Filter = MakeShared<FKzNotifyClassFilter>();
	Filter->BaseClass = BaseClass;

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowObjectRootClass = false;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bEnableClassDynamicLoading = true;
	Options.bExpandRootNodes = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
	Options.ClassFilters.Add(Filter);

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	return SNew(SBox)
		.MinDesiredWidth(280.f)
		.MaxDesiredHeight(400.f)
		[
			ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([this, TrackIndex, TimeSeconds](UClass* PickedClass)
			{
				FSlateApplication::Get().DismissAllMenus();
				AddEventAt(TrackIndex, PickedClass, TimeSeconds);
			}))
		];
}

void SKzDialogueTimeline::CreateNewNotify(int32 TrackIndex, UClass* ParentClass, float TimeSeconds)
{
	if (!ParentClass) { return; }

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAssetWithDialog(UBlueprint::StaticClass(), Factory);
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(NewAsset))
	{
		if (UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			AddEventAt(TrackIndex, GeneratedClass, TimeSeconds);
		}
	}
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeEventMenuForTrack(int32 TrackIndex, int32 EventIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	const bool bEnabled = T && T->Tracks.IsValidIndex(TrackIndex) && T->Tracks[TrackIndex].Events.IsValidIndex(EventIndex) && T->Tracks[TrackIndex].Events[EventIndex].bEnabled;

	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection(NAME_None, LOCTEXT("NotifyActions", "Notify"));
	{
		Menu.AddMenuEntry(
			bEnabled ? LOCTEXT("DisableNotify", "Disable") : LOCTEXT("EnableNotify", "Enable"),
			LOCTEXT("ToggleNotifyTip", "Mute or unmute this notify without deleting it."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, EventIndex]() { ToggleEventEnabled(TrackIndex, EventIndex); })));
		Menu.AddMenuEntry(
			LOCTEXT("CopyNotify", "Copy"),
			LOCTEXT("CopyNotifyTip", "Copy this notify so it can be pasted onto any line."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, EventIndex]() { CopyEvent(TrackIndex, EventIndex); })));
		Menu.AddMenuEntry(
			LOCTEXT("DuplicateNotify", "Duplicate"),
			LOCTEXT("DuplicateNotifyTip", "Add a copy of this notify on the same track."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, EventIndex]() { DuplicateEvent(TrackIndex, EventIndex); })));
		Menu.AddMenuEntry(
			LOCTEXT("DeleteNotify", "Delete"),
			LOCTEXT("DeleteNotifyTip", "Remove this notify from the track."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, EventIndex]() { RemoveEvent(TrackIndex, EventIndex); })));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

void SKzDialogueTimeline::SyncEventDetails()
{
	if (!EventDetailsView.IsValid()) { return; }

	HostInvalidationFramesPending = 2;

	FKzDialogueNotifyEvent* LiveEvent = SelectedEvent();
	if (!LiveEvent)
	{
		EventScope.Reset();
		ScopeTrack = INDEX_NONE;
		ScopeEvent = INDEX_NONE;
		EventDetailsView->SetStructureData(nullptr);
		return;
	}

	// A fresh scope (a copy of the event) on a new selection; otherwise keep the scope the view
	// is editing and just refresh it from the live event (covers drags and other edits).
	if (!EventScope.IsValid() || ScopeTrack != SelTrack || ScopeEvent != SelEvent)
	{
		EventScope = MakeShared<FStructOnScope>(FKzDialogueNotifyEvent::StaticStruct());
		ScopeTrack = SelTrack;
		ScopeEvent = SelEvent;
		EventDetailsView->SetStructureData(EventScope);
	}
	FKzDialogueNotifyEvent::StaticStruct()->CopyScriptStruct(EventScope->GetStructMemory(), LiveEvent);
}

bool SKzDialogueTimeline::PassesFilter(FName TrackName) const
{
	if (FilterText.IsEmpty()) { return true; }
	return TrackName.ToString().Contains(FilterText.ToString());
}

FName SKzDialogueTimeline::GetNewTrackName() const
{
	UKzDialogueTimeline* T = Timeline.Get();
	TArray<FName> Names;
	if (T)
	{
		for (const FKzDialogueNotifyTrack& Track : T->Tracks) { Names.Add(Track.Name); }
	}

	FName Candidate;
	int32 Index = 1;
	do { Candidate = *FString::FromInt(Index++); } while (Names.Contains(Candidate));
	return Candidate;
}

void SKzDialogueTimeline::AddTrack()
{
	EditTimeline(LOCTEXT("AddTrackTransaction", "Add Notify Track"), [this](UKzDialogueTimeline& T)
	{
		FKzDialogueNotifyTrack NewTrack;
		NewTrack.Name = GetNewTrackName();
		T.Tracks.Add(NewTrack);
		bNotifiesExpanded = true;
	});
}

void SKzDialogueTimeline::RemoveTrack(int32 TrackIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	// Keep at least one track; deleting the last one is what "Delete timeline" is for.
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || T->Tracks.Num() <= 1) { return; }

	EditTimeline(LOCTEXT("RemoveTrackTransaction", "Delete Notify Track"), [this, TrackIndex](UKzDialogueTimeline& Edited)
	{
		Edited.Tracks.RemoveAt(TrackIndex);
		if (SelTrack == TrackIndex) { SelTrack = INDEX_NONE; SelEvent = INDEX_NONE; }
		else if (SelTrack > TrackIndex) { --SelTrack; }
	});
}

void SKzDialogueTimeline::RenameTrack(int32 TrackIndex, const FText& NewName)
{
	UKzDialogueTimeline* T = Timeline.Get();
	const FName Name(*NewName.ToString());
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || T->Tracks[TrackIndex].Name == Name) { return; }

	// No rebuild: the committing name box already shows the new name and must outlive its own callback.
	EditTimeline(LOCTEXT("RenameTrackTransaction", "Rename Notify Track"), [TrackIndex, Name](UKzDialogueTimeline& Edited)
	{
		Edited.Tracks[TrackIndex].Name = Name;
	}, /*bRebuild=*/false);
}

void SKzDialogueTimeline::AddEventAt(int32 TrackIndex, UClass* NotifyClass, float TimeSeconds)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !NotifyClass) { return; }

	EditTimeline(LOCTEXT("AddEventTransaction", "Add Dialogue Notify"), [this, TrackIndex, NotifyClass, TimeSeconds](UKzDialogueTimeline& Edited)
	{
		FKzDialogueNotifyEvent NewEvent;
		NewEvent.Notify = NewObject<UKzDialogueNotifyBase>(&Edited, NotifyClass, NAME_None, RF_Transactional);
		if (FKzDialogueTimeSource_Relative* Rel = NewEvent.TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>())
		{
			Rel->Time = FMath::Max(0.f, TimeSeconds);

			// State notifies span a window, so give them a non-zero default duration that fits.
			if (NotifyClass->IsChildOf(UKzDialogueNotifyState::StaticClass()))
			{
				const float Remaining = FMath::Max(0.f, Duration() - Rel->Time);
				Rel->Duration = FMath::Clamp(0.5f, 0.1f, FMath::Max(0.1f, Remaining));
			}
		}

		SelTrack = TrackIndex;
		SelEvent = Edited.Tracks[TrackIndex].Events.Add(NewEvent);
	});
}

void SKzDialogueTimeline::RemoveSelected()
{
	RemoveEvent(SelTrack, SelEvent);
}

void SKzDialogueTimeline::RemoveEvent(int32 TrackIndex, int32 EventIndex)
{
	if (!FindEvent(TrackIndex, EventIndex)) { return; }

	EditTimeline(LOCTEXT("DeleteEventTransaction", "Delete Dialogue Notify"), [this, TrackIndex, EventIndex](UKzDialogueTimeline& Edited)
	{
		Edited.Tracks[TrackIndex].Events.RemoveAt(EventIndex);
		if (SelTrack == TrackIndex)
		{
			if (SelEvent == EventIndex) { SelEvent = INDEX_NONE; }
			else if (SelEvent > EventIndex) { --SelEvent; }
		}
	});
}

void SKzDialogueTimeline::ToggleEventEnabled(int32 TrackIndex, int32 EventIndex)
{
	if (!FindEvent(TrackIndex, EventIndex)) { return; }

	EditTimeline(LOCTEXT("ToggleEnabledTransaction", "Toggle Dialogue Notify"), [TrackIndex, EventIndex](UKzDialogueTimeline& Edited)
	{
		bool& bEnabled = Edited.Tracks[TrackIndex].Events[EventIndex].bEnabled;
		bEnabled = !bEnabled;
	});
}

void SKzDialogueTimeline::DuplicateEvent(int32 TrackIndex, int32 EventIndex)
{
	if (!FindEvent(TrackIndex, EventIndex)) { return; }

	EditTimeline(LOCTEXT("DuplicateEventTransaction", "Duplicate Dialogue Notify"), [this, TrackIndex, EventIndex](UKzDialogueTimeline& Edited)
	{
		// Read every field of the source before Add (which may reallocate the array).
		const FKzDialogueNotifyEvent& Src = Edited.Tracks[TrackIndex].Events[EventIndex];
		FKzDialogueNotifyEvent NewEvent;
		NewEvent.TimeSource = Src.TimeSource;
		NewEvent.bFireIfSkipped = Src.bFireIfSkipped;
		NewEvent.bEnabled = Src.bEnabled;
		NewEvent.Notify = Src.Notify ? DuplicateObject<UKzDialogueNotifyBase>(Src.Notify, &Edited) : nullptr;

		// Nudge the copy off the original so it is selectable.
		if (FKzDialogueTimeSource_Relative* Rel = NewEvent.TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>())
		{
			const float Offset = Rel->bNormalized ? 0.02f : 0.1f;
			const float MaxTime = Rel->bNormalized ? 1.f : Duration();
			Rel->Time = FMath::Min(Rel->Time + Offset, MaxTime);
		}

		SelTrack = TrackIndex;
		SelEvent = Edited.Tracks[TrackIndex].Events.Add(NewEvent);
	});
}

void SKzDialogueTimeline::CopyEvent(int32 TrackIndex, int32 EventIndex)
{
	const FKzDialogueNotifyEvent* Src = FindEvent(TrackIndex, EventIndex);
	if (!Src) { return; }

	if (UKzDialogueNotifyBase* Old = GClipboardNotify.Get()) { Old->RemoveFromRoot(); }
	GClipboardNotify.Reset();
	GClipboardTimeSource = Src->TimeSource;
	GClipboardFireIfSkipped = Src->bFireIfSkipped;
	GClipboardEnabled = Src->bEnabled;
	if (Src->Notify)
	{
		// Duplicate into the transient package and root it so the clipboard survives until replaced.
		UKzDialogueNotifyBase* Copy = DuplicateObject<UKzDialogueNotifyBase>(Src->Notify, GetTransientPackage());
		Copy->AddToRoot();
		GClipboardNotify = Copy;
	}
}

void SKzDialogueTimeline::PasteEvent(int32 TrackIndex, float TimeSeconds)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !GClipboardNotify.IsValid()) { return; }

	EditTimeline(LOCTEXT("PasteEventTransaction", "Paste Dialogue Notify"), [this, TrackIndex, TimeSeconds](UKzDialogueTimeline& Edited)
	{
		FKzDialogueNotifyEvent NewEvent;
		NewEvent.TimeSource = GClipboardTimeSource;
		NewEvent.bFireIfSkipped = GClipboardFireIfSkipped;
		NewEvent.bEnabled = GClipboardEnabled;
		NewEvent.Notify = DuplicateObject<UKzDialogueNotifyBase>(GClipboardNotify.Get(), &Edited);

		// Place it at the cursor time (converting to a fraction when the source is normalized).
		if (FKzDialogueTimeSource_Relative* Rel = NewEvent.TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>())
		{
			Rel->Time = Rel->bNormalized ? (Duration() > 0.f ? FMath::Clamp(TimeSeconds / Duration(), 0.f, 1.f) : 0.f) : FMath::Max(0.f, TimeSeconds);
		}

		SelTrack = TrackIndex;
		SelEvent = Edited.Tracks[TrackIndex].Events.Add(NewEvent);
	});
}

void SKzDialogueTimeline::SetSelection(int32 TrackIndex, int32 EventIndex)
{
	// No rebuild: the track reads selection as an attribute and repaints itself, so the
	// mouse capture that drives a drag survives. The inline editor refreshes on Rebuild,
	// fired from the track's OnInteractionEnd when the mouse is released.
	SelTrack = TrackIndex;
	SelEvent = EventIndex;

	// Any click on a track takes keyboard focus, so the shortcuts work after clicking empty space too.
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
}

void SKzDialogueTimeline::RequestNotifySelection(const FGuid& OwningLineId, int32 TrackIndex, int32 EventIndex)
{
	GPendingSelectionLineId = OwningLineId;
	GPendingSelectionTrack = TrackIndex;
	GPendingSelectionEvent = EventIndex;
}

void SKzDialogueTimeline::BeginRetime()
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T) { return; }
	// A drag spans many frames, so it holds its own transaction instead of going through EditTimeline.
	DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("RetimeEventTransaction", "Retime Dialogue Notify"));
	T->Modify();
}

void SKzDialogueTimeline::RetimeEvent(int32 TrackIndex, int32 EventIndex, float StartSeconds, float EndSeconds)
{
	FKzDialogueNotifyEvent* Event = FindEvent(TrackIndex, EventIndex);
	FKzDialogueTimeSource_Relative* Rel = Event ? Event->TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>() : nullptr;
	if (!Rel) { return; }

	if (!DragSnapshot.IsValid())
	{
		DragSnapshot = Event->TimeSource;
		DragSnapshotTrack = TrackIndex;
		DragSnapshotEvent = EventIndex;
	}

	const float Dur = Duration();
	StartSeconds = FMath::Clamp(StartSeconds, 0.f, Dur);
	EndSeconds = FMath::Clamp(EndSeconds, StartSeconds, Dur);
	const float LenSeconds = EndSeconds - StartSeconds;

	if (Rel->bNormalized)
	{
		Rel->Time = Dur > UE_KINDA_SMALL_NUMBER ? StartSeconds / Dur : 0.f;
		Rel->Duration = Dur > UE_KINDA_SMALL_NUMBER ? LenSeconds / Dur : 0.f;
	}
	else
	{
		Rel->Time = StartSeconds;
		Rel->Duration = LenSeconds;
	}
	Modified();
}

void SKzDialogueTimeline::EndRetime()
{
	DragTransaction.Reset();
	DragSnapshot.Reset();
	DragHoverTrack = INDEX_NONE;
	Rebuild();
}

void SKzDialogueTimeline::CancelRetime()
{
	// Cancelling the transaction only drops it; the dragged time is put back by hand.
	if (FKzDialogueNotifyEvent* Event = FindEvent(DragSnapshotTrack, DragSnapshotEvent); Event && DragSnapshot.IsValid())
	{
		Event->TimeSource = DragSnapshot;
		Modified();
	}
	if (DragTransaction.IsValid())
	{
		DragTransaction->Cancel();
		DragTransaction.Reset();
	}

	// The dragging track loses its capture and finishes through EndRetime, with no move to another track.
	FSlateApplication::Get().ReleaseAllPointerCapture();
}

int32 SKzDialogueTimeline::TrackAtRowOffset(int32 FromTrack, int32 RowDelta) const
{
	const int32 Row = VisibleTracks.IndexOfByKey(FromTrack);
	if (Row == INDEX_NONE) { return FromTrack; }
	return VisibleTracks[FMath::Clamp(Row + RowDelta, 0, VisibleTracks.Num() - 1)];
}

void SKzDialogueTimeline::MoveEventToTrack(int32 FromTrack, int32 EventIndex, int32 ToTrack)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || T->Tracks.Num() == 0) { return; }
	ToTrack = FMath::Clamp(ToTrack, 0, T->Tracks.Num() - 1);
	if (FromTrack == ToTrack || !FindEvent(FromTrack, EventIndex)) { return; }

	EditTimeline(LOCTEXT("MoveEventToTrackTransaction", "Move Notify To Track"), [this, FromTrack, EventIndex, ToTrack](UKzDialogueTimeline& Edited)
	{
		const FKzDialogueNotifyEvent Moved = Edited.Tracks[FromTrack].Events[EventIndex];
		Edited.Tracks[FromTrack].Events.RemoveAt(EventIndex);
		SelTrack = ToTrack;
		SelEvent = Edited.Tracks[ToTrack].Events.Add(Moved);
	});
}

void SKzDialogueTimeline::OnEventDetailsChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Spin box drags commit once at the end; the scope is written back to the event it was made
	// from, which a click on another marker may no longer be the selected one.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive || !EventScope.IsValid() || !FindEvent(ScopeTrack, ScopeEvent)) { return; }

	EditTimeline(LOCTEXT("EditEventTransaction", "Edit Dialogue Notify"), [this](UKzDialogueTimeline& Edited)
	{
		FKzDialogueNotifyEvent* Event = FindEvent(ScopeTrack, ScopeEvent);
		FKzDialogueNotifyEvent::StaticStruct()->CopyScriptStruct(Event, EventScope->GetStructMemory());

		// A notify class picked in the details is created without our outer; re-home it so it persists.
		if (Event->Notify && Event->Notify->GetOuter() != &Edited)
		{
			Event->Notify = DuplicateObject<UKzDialogueNotifyBase>(Event->Notify, &Edited);
		}
	});
}

#undef LOCTEXT_NAMESPACE