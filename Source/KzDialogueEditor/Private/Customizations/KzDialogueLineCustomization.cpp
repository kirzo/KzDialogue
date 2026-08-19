// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineCustomization.h"
#include "KzDialogueTypes.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Utils/KzEditorUtils.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SKzAudioRangeStrip.h"
#include "Widgets/SKzDialogueTimeline.h"

#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineCustomization"

namespace
{
	/** One insertable token in the picker: "{Kirzo}" or "{Kirzo:given}", with its live resolution preview and the owning asset's type for filtering. */
	struct FKzTokenPickerRow
	{
		FString TokenText;
		FText Preview;
		FString TypeName;
		FString AssetPath;
	};

	/**
	 * Token picker content: a searchable, type-filterable flat list of every named asset's
	 * token and parts, each with a live resolution preview. Data is snapshotted up front so
	 * the widget holds plain values, never asset pointers. Enter inserts the top match;
	 * clicking a row (even the already-selected one) inserts it.
	 */
	TSharedRef<SWidget> BuildTokenPickerMenu(TSharedRef<IPropertyHandle> TextHandle, TSharedPtr<SSearchBox>& OutSearchBox)
	{
		TSharedRef<TArray<TSharedPtr<FKzTokenPickerRow>>> AllRows = MakeShared<TArray<TSharedPtr<FKzTokenPickerRow>>>();
		TSharedRef<TArray<FString>> TypeNames = MakeShared<TArray<FString>>();
		{
			const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			TArray<FAssetData> NamedAssets;
			Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);

			TSet<FName> Seen;
			for (const FAssetData& Data : NamedAssets)
			{
				FName Token;
				if (!Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), Token) || Token.IsNone() || Seen.Contains(Token)) { continue; }
				Seen.Add(Token);

				const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Data.ToSoftObjectPath().TryLoad());
				if (!Named) { continue; }

				const FString TypeName = Named->GetClass()->GetDisplayNameText().ToString();
				TypeNames->AddUnique(TypeName);

				auto AddRow = [&](const FString& TokenText, const FText& Preview)
				{
					TSharedPtr<FKzTokenPickerRow> Row = MakeShared<FKzTokenPickerRow>();
					Row->TokenText = TokenText;
					Row->Preview = Preview;
					Row->TypeName = TypeName;
					Row->AssetPath = Data.GetObjectPathString();
					AllRows->Add(Row);
				};

				AddRow(FString::Printf(TEXT("{%s}"), *Token.ToString()), Named->ResolveName());
				for (const FName Part : Named->GetNameParts())
				{
					AddRow(FString::Printf(TEXT("{%s:%s}"), *Token.ToString(), *Part.ToString()), Named->ResolveName(Part));
				}
			}
			AllRows->Sort([](const TSharedPtr<FKzTokenPickerRow>& A, const TSharedPtr<FKzTokenPickerRow>& B) { return A->TokenText < B->TokenText; });
			TypeNames->Sort();
		}

		// Shared picker state: the search string, the type filter ("" = all) and the visible slice.
		TSharedRef<FString> SearchFilter = MakeShared<FString>();
		TSharedRef<FString> TypeFilter = MakeShared<FString>();
		TSharedRef<TArray<TSharedPtr<FKzTokenPickerRow>>> VisibleRows = MakeShared<TArray<TSharedPtr<FKzTokenPickerRow>>>(*AllRows);

		auto InsertToken = [TextHandle](const FString& TokenText)
		{
			FText Current;
			TextHandle->GetValue(Current);
			TextHandle->SetValue(FText::FromString(Current.ToString() + TokenText));
			FSlateApplication::Get().DismissAllMenus();
		};

		TSharedRef<SListView<TSharedPtr<FKzTokenPickerRow>>> ListView =
			SNew(SListView<TSharedPtr<FKzTokenPickerRow>>)
				.ListItemsSource(&VisibleRows.Get())
				.SelectionMode(ESelectionMode::Single)
				// Click commits even when the item was already selected (selection alone does not).
				.OnMouseButtonClick_Lambda([InsertToken](TSharedPtr<FKzTokenPickerRow> Row)
				{
					if (Row.IsValid()) { InsertToken(Row->TokenText); }
				})
				.OnGenerateRow_Lambda([](TSharedPtr<FKzTokenPickerRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
				{
					return SNew(STableRow<TSharedPtr<FKzTokenPickerRow>>, OwnerTable)
						.ToolTipText(FText::Format(LOCTEXT("TokenRowTip", "Resolves to: {0}\n{1}\n{2}"), Row->Preview, FText::FromString(Row->TypeName), FText::FromString(Row->AssetPath)))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(Row->TokenText))
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 2.0f, 4.0f, 2.0f)
							[
								SNew(STextBlock)
									.Text(Row->Preview)
									.Justification(ETextJustify::Right)
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							]
						];
				});

		auto RebuildVisible = [AllRows, VisibleRows, SearchFilter, TypeFilter, ListViewWeak = TWeakPtr<SListView<TSharedPtr<FKzTokenPickerRow>>>(ListView)]()
		{
			VisibleRows->Reset();
			for (const TSharedPtr<FKzTokenPickerRow>& Row : *AllRows)
			{
				if (!TypeFilter->IsEmpty() && Row->TypeName != *TypeFilter) { continue; }
				if (!SearchFilter->IsEmpty() && !Row->TokenText.Contains(*SearchFilter, ESearchCase::IgnoreCase) && !Row->Preview.ToString().Contains(*SearchFilter, ESearchCase::IgnoreCase)) { continue; }
				VisibleRows->Add(Row);
			}
			if (TSharedPtr<SListView<TSharedPtr<FKzTokenPickerRow>>> Pinned = ListViewWeak.Pin())
			{
				Pinned->RequestListRefresh();
			}
		};

		TSharedRef<SSearchBox> SearchBox = SNew(SSearchBox)
			.HintText(LOCTEXT("TokenSearchHint", "Search tokens..."))
			.OnTextChanged_Lambda([SearchFilter, RebuildVisible](const FText& NewText)
			{
				*SearchFilter = NewText.ToString();
				RebuildVisible();
			})
			.OnTextCommitted_Lambda([VisibleRows, InsertToken](const FText&, ETextCommit::Type CommitType)
			{
				// Enter takes the top match, so search-and-enter needs no mouse at all.
				if (CommitType == ETextCommit::OnEnter && VisibleRows->Num() > 0)
				{
					InsertToken((*VisibleRows)[0]->TokenText);
				}
			});
		OutSearchBox = SearchBox;

		return SNew(SBox).WidthOverride(400.0f).HeightOverride(320.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SearchBox
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SComboButton)
						.ToolTipText(LOCTEXT("TokenTypeFilterTip", "Show only tokens of one asset type."))
						.OnGetMenuContent_Lambda([TypeNames, TypeFilter, RebuildVisible]()
						{
							// Close-self-only: picking a type must not dismiss the whole picker popup.
							FMenuBuilder TypeMenu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bInCloseSelfOnly=*/true);
							TypeMenu.AddMenuEntry(LOCTEXT("AllTokenTypes", "All types"), FText::GetEmpty(), FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([TypeFilter, RebuildVisible]() { TypeFilter->Reset(); RebuildVisible(); })));
							for (const FString& TypeName : *TypeNames)
							{
								TypeMenu.AddMenuEntry(FText::FromString(TypeName), FText::GetEmpty(), FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([TypeFilter, RebuildVisible, TypeName]() { *TypeFilter = TypeName; RebuildVisible(); })));
							}
							return TypeMenu.MakeWidget();
						})
						.ButtonContent()
						[
							SNew(STextBlock).Text_Lambda([TypeFilter]() { return TypeFilter->IsEmpty() ? LOCTEXT("AllTokenTypes", "All types") : FText::FromString(*TypeFilter); })
						]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f, 0.0f, 4.0f, 4.0f)
			[
				AllRows->IsEmpty()
					? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NoNamedAssets", "No named assets: set Token on a Speaker or Word asset first")).ColorAndOpacity(FSlateColor::UseSubduedForeground()))
					: StaticCastSharedRef<SWidget>(ListView)
			]
		];
	}
}

TSharedRef<IPropertyTypeCustomization> FKzDialogueLineCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueLineCustomization>();
}

FKzDialogueLineCustomization::~FKzDialogueLineCustomization()
{
	StopRangeAudition();
}

void FKzDialogueLineCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		];
}

void FKzDialogueLineCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	// Editable line fields (the runtime-only Timeline pointer is not among them). Fields with
	// a third category segment (Audio, Timing, Playback, Localization) render as groups.
	// The range fields are skipped here and re-added by AddAudioRangeRow so they sit at the
	// bottom of the Audio group, right above the visual strip they drive. The Text row is
	// claimed to gain the named-asset token picker without moving from its place.
	TMap<FString, IDetailGroup*> Groups;
	const TSet<FName> RangeFields{ GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioStartTime), GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime) };
	FKzPropertyHandleUtils::AddChildrenGroupedByCategory(StructBuilder, StructPropertyHandle, RangeFields, &Groups,
		[this](IDetailChildrenBuilder& Builder, TSharedRef<IPropertyHandle> Child)
		{
			if (Child->GetProperty() && Child->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, Text))
			{
				AddTextRowWithTokenPicker(Builder, Child);
				return true;
			}
			return false;
		});

	// Visual playback-range editor inside the Audio group (plain waves only: a cue or
	// metasound wrapper has no single waveform to draw).
	if (IDetailGroup* const* AudioGroup = Groups.Find(TEXT("Audio")))
	{
		AddAudioRangeRow(**AudioGroup);
	}

	// The strip caches the wave it was built from: rebuild the rows when the audio changes.
	if (TSharedPtr<IPropertyHandle> AudioHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, Audio)))
	{
		AudioHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities = TWeakPtr<IPropertyUtilities>(PropertyUtilities)]()
		{
			if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
			{
				Utilities->ForceRefresh();
			}
		}));
	}

	// Per-line timeline, authored on the asset and presented here by line id.
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	UKzDialogueTimeline* Timeline = (Asset && LineId.IsValid()) ? Asset->FindTimelineForLine(LineId) : nullptr;

	if (Timeline)
	{
		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.NameContent()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("TimelineLabel", "Timeline"))
			]
			.ValueContent()
			[
				SNew(SButton)
					.Text(LOCTEXT("DeleteTimeline", "Delete timeline"))
					.ToolTipText(LOCTEXT("DeleteTimelineTip", "Removes this line's notify timeline from the asset."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnDeleteTimelineClicked))
			];

		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.WholeRowContent()
			[
				SNew(SKzDialogueTimeline, Timeline)
					.DisplayDuration_Lambda([this]() { return GetDisplayDuration(); })
					.OnModified_Lambda([this]()
					{
						if (UKzDialogueAsset* OwningAsset = ResolveOwningAsset()) { OwningAsset->MarkPackageDirty(); }
					})
					.OnRequestHostRefresh_Lambda([this]()
					{
						// RefreshTree only: re-measures the hosting details tree (fixing its scroll
						// after this row grows) without re-running customizations.
						if (PropertyUtilities.IsValid()) { PropertyUtilities->RequestRefresh(); }
					})
			];
	}
	else
	{
		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.NameContent()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("TimelineLabel", "Timeline"))
			]
			.ValueContent()
			[
				SNew(SButton)
					.Text(LOCTEXT("CreateTimeline", "Create timeline"))
					.ToolTipText(LOCTEXT("CreateTimelineTip", "Adds a notify timeline to this line (stored on the asset)."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnCreateTimelineClicked))
			];
	}
}

void FKzDialogueLineCustomization::AddTextRowWithTokenPicker(IDetailChildrenBuilder& StructBuilder, TSharedRef<IPropertyHandle> TextHandle)
{
	// Default text editor plus the token picker; picked tokens append at the end of the text.
	// The holder wires the lazily-built menu's search box as the combo's focus target.
	TSharedRef<TWeakPtr<SComboButton>> ComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ToolTipText(LOCTEXT("TokenPickerTip", "Insert a named-asset token: it resolves to the thing's localized name when the line plays."))
		.OnGetMenuContent_Lambda([TextHandle, ComboHolder]()
		{
			TSharedPtr<SSearchBox> SearchBox;
			TSharedRef<SWidget> Menu = BuildTokenPickerMenu(TextHandle, SearchBox);
			if (TSharedPtr<SComboButton> Pinned = ComboHolder->Pin())
			{
				Pinned->SetMenuContentWidgetToFocus(SearchBox);
			}
			return Menu;
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(INVTEXT("{}"))
		];
	*ComboHolder = Combo;

	StructBuilder.AddProperty(TextHandle).CustomWidget()
		.NameContent()
		[
			TextHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(325.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				TextHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				Combo
			]
		];
}

void FKzDialogueLineCustomization::AddAudioRangeRow(IDetailGroup& AudioGroup)
{
	if (!StructHandle.IsValid())
	{
		return;
	}

	// The range fields skipped by the grouped walk, placed here right above their strip.
	TSharedPtr<IPropertyHandle> StartHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioStartTime));
	TSharedPtr<IPropertyHandle> EndHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime));
	if (StartHandle.IsValid()) { AudioGroup.AddPropertyRow(StartHandle.ToSharedRef()); }
	if (EndHandle.IsValid()) { AudioGroup.AddPropertyRow(EndHandle.ToSharedRef()); }

	void* RawData = nullptr;
	if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return;
	}
	const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
	USoundWave* Wave = Cast<USoundWave>(Line->Audio.LoadSynchronous());
	if (!Wave)
	{
		return;
	}

	AudioGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.f))
					.ToolTipText(LOCTEXT("PlayRangeTip", "Preview exactly what the game will play: the custom range when set, the whole audio otherwise."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnPlayRangeClicked))
					[
						SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
						[
							SNew(SImage)
								.Image_Lambda([this]() { return FAppStyle::GetBrush(IsAuditioningRange() ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"); })
								.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SKzAudioRangeStrip, Wave)
					.StartHandle(StartHandle)
					.EndHandle(EndHandle)
			]
		];
}

bool FKzDialogueLineCustomization::IsAuditioningRange() const
{
	return RangePreviewAudio.IsValid() && RangePreviewAudio->IsPlaying();
}

void FKzDialogueLineCustomization::StopRangeAudition()
{
	if (RangeStopTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RangeStopTicker);
		RangeStopTicker.Reset();
	}
	if (GEditor)
	{
		GEditor->ResetPreviewAudioComponent();
	}
	RangePreviewAudio = nullptr;
}

FReply FKzDialogueLineCustomization::OnPlayRangeClicked()
{
	// Toggle off when this row's preview is the one playing.
	if (IsAuditioningRange())
	{
		StopRangeAudition();
		return FReply::Handled();
	}

	StopRangeAudition();

	void* RawData = nullptr;
	if (!StructHandle.IsValid() || StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData || !GEditor)
	{
		return FReply::Handled();
	}
	const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
	USoundBase* Sound = Line->Audio.LoadSynchronous();
	if (!Sound)
	{
		return FReply::Handled();
	}

	UAudioComponent* Preview = GEditor->PlayPreviewSound(Sound);
	if (!Preview)
	{
		return FReply::Handled();
	}
	RangePreviewAudio = Preview;

	// Same behavior as the runtime player: start at the offset, cut at the end time.
	const float Start = FMath::Max(0.0f, Line->AudioStartTime);
	if (Start > 0.0f)
	{
		Preview->Stop();
		Preview->Play(Start);
	}
	if (Line->AudioEndTime > 0.0f)
	{
		const float StopIn = FMath::Max(0.05f, Line->AudioEndTime - Start);
		RangeStopTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakAudio = RangePreviewAudio](float)
		{
			if (UAudioComponent* Audio = WeakAudio.Get())
			{
				Audio->FadeOut(0.1f, 0.0f);
			}
			return false;   // one-shot
		}), StopIn);
	}

	return FReply::Handled();
}

UKzDialogueAsset* FKzDialogueLineCustomization::ResolveOwningAsset() const
{
	if (!PropertyUtilities.IsValid()) { return nullptr; }
	for (const TWeakObjectPtr<UObject>& WeakObj : PropertyUtilities->GetSelectedObjects())
	{
		if (UObject* Obj = WeakObj.Get())
		{
			if (UKzDialogueAsset* Direct = Cast<UKzDialogueAsset>(Obj)) { return Direct; }
			if (UKzDialogueAsset* Typed = Obj->GetTypedOuter<UKzDialogueAsset>()) { return Typed; }
		}
	}
	return nullptr;
}

FGuid FKzDialogueLineCustomization::GetLineId() const
{
	if (!StructHandle.IsValid()) { return FGuid(); }
	void* RawData = nullptr;
	if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return FGuid();
	}
	return reinterpret_cast<const FKzDialogueLine*>(RawData)->LineId;
}

float FKzDialogueLineCustomization::GetDisplayDuration() const
{
	// Mirror the runtime: resolve the audio length, then apply the line's DurationMode
	// (UKzDialoguePlayer::ResolveLineDuration goes through the same FKzDialogueLine::ResolveDuration).
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const float Default = Settings ? Settings->DefaultDuration : 2.5f;

	if (StructHandle.IsValid())
	{
		void* RawData = nullptr;
		if (StructHandle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
		{
			const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
			float AudioLength = 0.f;
			if (USoundBase* Sound = Line->Audio.LoadSynchronous())
			{
				AudioLength = Line->ResolveAudioLength(Sound->GetDuration());
			}
			return FMath::Max(0.1f, Line->ResolveDuration(AudioLength, Default));
		}
	}

	return Default;
}

FReply FKzDialogueLineCustomization::OnCreateTimelineClicked()
{
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	if (!Asset || !LineId.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateTimelineTransaction", "Create Dialogue Timeline"));
	Asset->Modify();

	UKzDialogueTimeline* NewTimeline = NewObject<UKzDialogueTimeline>(Asset, NAME_None, RF_Transactional);
	NewTimeline->OwningLineId = LineId;

	// Start with one track so the line has somewhere to drop notifies right away.
	FKzDialogueNotifyTrack DefaultTrack;
	DefaultTrack.Name = TEXT("1");
	NewTimeline->Tracks.Add(DefaultTrack);

	Asset->Timelines.Add(NewTimeline);

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
	return FReply::Handled();
}

FReply FKzDialogueLineCustomization::OnDeleteTimelineClicked()
{
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	if (!Asset || !LineId.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteTimelineTransaction", "Delete Dialogue Timeline"));
	Asset->Modify();
	Asset->Timelines.RemoveAll([LineId](const UKzDialogueTimeline* Timeline)
	{
		return Timeline && Timeline->OwningLineId == LineId;
	});

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE