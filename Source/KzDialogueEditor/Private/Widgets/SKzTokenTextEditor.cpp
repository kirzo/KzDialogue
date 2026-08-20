// Copyright 2026 kirzo

#include "Widgets/SKzTokenTextEditor.h"
#include "Widgets/SKzTokenPicker.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "PropertyHandle.h"
#include "STextPropertyEditableTextBox.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SKzTokenTextEditor"

namespace KzTokenTextEditorInternal
{
	/** StaticStableTextId is protected on IEditableTextProperty; this derived shim exposes it. */
	class FStableTextIdShim : public IEditableTextProperty
	{
	public:
		static void GetStable(UObject* TextOuter, ETextPropertyEditAction EditAction, const FString& InSource, const FString& ProposedNamespace, const FString& ProposedKey, FString& OutNamespace, FString& OutKey)
		{
			StaticStableTextId(TextOuter, EditAction, InSource, ProposedNamespace, ProposedKey, OutNamespace, OutKey);
		}
	};

	/** Stable identity through the property's outer package; without one, the proposals pass through (key falling back to a fresh GUID). */
	static void GetStableTextId(const TSharedPtr<IPropertyHandle>& TextHandle, IEditableTextProperty::ETextPropertyEditAction EditAction, const FString& InSource, const FString& ProposedNamespace, const FString& ProposedKey, FString& OutNamespace, FString& OutKey)
	{
		TArray<UObject*> Outers;
		TextHandle->GetOuterObjects(Outers);
		if (Outers.Num() > 0 && Outers[0])
		{
			FStableTextIdShim::GetStable(Outers[0], EditAction, InSource, ProposedNamespace, ProposedKey, OutNamespace, OutKey);
			return;
		}

		OutNamespace = ProposedNamespace;
		OutKey = ProposedKey.IsEmpty() ? FGuid::NewGuid().ToString() : ProposedKey;
	}
}

void SKzTokenTextEditor::Construct(const FArguments& InArgs, TSharedRef<IPropertyHandle> InTextHandle)
{
	TextHandle = InTextHandle;
	bManagedLocalizationKeys = InArgs._bManagedLocalizationKeys;

	FText InitialText;
	InTextHandle->GetValue(InitialText);
	TextSnapshot = InitialText.ToString();

	// Commits on focus loss, like the stock multiline editor; Enter inside the text inserts
	// a newline.
	TextEditor = SNew(SMultiLineEditableTextBox)
		.Text(InitialText)
		.AutoWrapText(true)
		.OnTextChanged_Lambda([this](const FText& NewText) { OnTextChangedInternal(NewText); })
		// The commit must NOT cancel the autocomplete session: clicking a popup row moves the
		// focus (committing) BEFORE the click lands; the session ends via OnMenuOpenChanged.
		.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
		{
			FText Current;
			TextHandle->GetValue(Current);
			if (!NewText.ToString().Equals(Current.ToString(), ESearchCase::CaseSensitive))
			{
				TextHandle->SetValue(NewText);
			}
		})
		.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent) { return OnKeyDownInternal(KeyEvent); });

	InTextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSPLambda(this, [this]()
	{
		if (TextEditor.IsValid())
		{
			FText Value;
			TextHandle->GetValue(Value);
			if (!Value.ToString().Equals(TextEditor->GetText().ToString(), ESearchCase::CaseSensitive))
			{
				TextEditor->SetText(Value);
			}
		}
	}));

	// The autocomplete popup anchors under the editor; it never takes the keyboard focus,
	// the editor routes the keyboard to it while a session is active.
	TokenMenuAnchor = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		// Whatever closes the popup (outside click, accept, Esc) also ends the session state.
		.OnMenuOpenChanged_Lambda([this](bool bOpen)
		{
			if (!bOpen)
			{
				AutocompleteBraceIndex = INDEX_NONE;
				AutocompleteFragmentLen = 0;
			}
		})
		.OnGetMenuContent_Lambda([this]()
		{
			return SAssignNew(AutocompletePicker, SKzTokenPicker)
				.bAutocompleteMode(true)
				.OnTokenChosen_Lambda([this](const FString& TokenText) { AcceptTokenAutocomplete(TokenText); });
		})
		[
			TextEditor.ToSharedRef()
		];

	// The "{}" button opens the full browser; the holder wires its search box as the focus target.
	TSharedRef<TWeakPtr<SComboButton>> ComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ToolTipText(LOCTEXT("TokenPickerTip", "Insert a named-asset token at the cursor: it resolves to the thing's localized name when the text displays. Typing '{' in the text offers the same list inline."))
		.OnGetMenuContent_Lambda([this, ComboHolder]()
		{
			TSharedRef<SKzTokenPicker> Picker = SNew(SKzTokenPicker)
				.OnTokenChosen_Lambda([this](const FString& TokenText)
				{
					if (TextEditor.IsValid())
					{
						TextEditor->InsertTextAtCursor(TokenText);
						TextHandle->SetValue(TextEditor->GetText());
					}
					FSlateApplication::Get().DismissAllMenus();
				});
			if (TSharedPtr<SComboButton> Pinned = ComboHolder->Pin())
			{
				Pinned->SetMenuContentWidgetToFocus(Picker->GetWidgetToFocus());
			}
			return StaticCastSharedRef<SWidget>(Picker);
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(INVTEXT("{}"))
		];
	*ComboHolder = Combo;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			TokenMenuAnchor.ToSharedRef()
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			Combo
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeLocalizationControl()
		]
	];
}

TSharedRef<SWidget> SKzTokenTextEditor::MakeLocalizationControl()
{
	TSharedRef<SWidget> Globe = SNew(SImage)
		.Image(FAppStyle::GetBrush("Icons.Localization"))
		.ColorAndOpacity(FSlateColor::UseForeground());

	if (bManagedLocalizationKeys)
	{
		// Keys are the host's business (the dialogue asset re-anchors them to stable keys),
		// so the control is a bare gather on/off toggle.
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText_Lambda([this]()
			{
				return IsLocalizable()
					? LOCTEXT("LocalizableTip", "Localizable: this text is gathered for translation. Click to exclude it.")
					: LOCTEXT("NotLocalizableTip", "Not localizable: this text is skipped by the localization gather. Click to make it localizable.");
			})
			.IsChecked_Lambda([this]() { return IsLocalizable() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetLocalizable(NewState == ECheckBoxState::Checked); })
			[
				Globe
			];
	}

	// Free keys: the stock FText widget's identity controls in a flyout.
	return SNew(SComboButton)
		.ToolTipText(LOCTEXT("LocMenuTip", "Localization: toggle gathering and edit this text's namespace and key."))
		.OnGetMenuContent_Lambda([this]()
		{
			return SNew(SBox).WidthOverride(320.0f).Padding(8.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SCheckBox)
							.IsChecked_Lambda([this]() { return IsLocalizable() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { SetLocalizable(NewState == ECheckBoxState::Checked); })
							[
								SNew(STextBlock).Text(LOCTEXT("LocalizableLabel", "Localizable"))
							]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(70.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("NamespaceLabel", "Namespace"))
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
								.Text(this, &SKzTokenTextEditor::GetNamespaceValue)
								.OnTextCommitted(this, &SKzTokenTextEditor::OnNamespaceCommitted)
								.IsEnabled_Lambda([this]() { return IsLocalizable(); })
								.SelectAllTextWhenFocused(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(70.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("KeyLabel", "Key"))
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
								.Text(this, &SKzTokenTextEditor::GetKeyValue)
								.OnTextCommitted(this, &SKzTokenTextEditor::OnKeyCommitted)
								.IsEnabled_Lambda([this]() { return IsLocalizable(); })
								.SelectAllTextWhenFocused(true)
						]
					]
				];
		})
		.ButtonContent()
		[
			Globe
		];
}

bool SKzTokenTextEditor::IsLocalizable() const
{
	FText Current;
	TextHandle->GetValue(Current);
	return !Current.IsCultureInvariant();
}

void SKzTokenTextEditor::SetLocalizable(bool bLocalizable)
{
	FText Current;
	TextHandle->GetValue(Current);
	if (bLocalizable == !Current.IsCultureInvariant()) { return; }

	// Reads go through the SOURCE string: the display string of a keyed text can be a
	// resolved translation.
	const FString* Source = FTextInspector::GetSourceString(Current);
	const FString SourceString = Source ? *Source : FString();

	if (!bLocalizable)
	{
		TextHandle->SetValue(FText::AsCultureInvariant(SourceString));
		return;
	}

	if (bManagedLocalizationKeys)
	{
		// Keyless on purpose: the host re-anchors it to its stable key.
		TextHandle->SetValue(FText::FromString(SourceString));
		return;
	}

	// Stock behavior: a stable package-bound identity right away.
	FString NewNamespace;
	FString NewKey;
	KzTokenTextEditorInternal::GetStableTextId(TextHandle, IEditableTextProperty::ETextPropertyEditAction::EditedKey, SourceString, FString(), FString(), NewNamespace, NewKey);
	TextHandle->SetValue(FText::AsLocalizable_Advanced(NewNamespace, NewKey, SourceString));
}

FText SKzTokenTextEditor::GetNamespaceValue() const
{
	FText Current;
	TextHandle->GetValue(Current);
	return FText::AsCultureInvariant(TextNamespaceUtil::StripPackageNamespace(FTextInspector::GetNamespace(Current).Get(FString())));
}

FText SKzTokenTextEditor::GetKeyValue() const
{
	FText Current;
	TextHandle->GetValue(Current);
	return FText::AsCultureInvariant(FTextInspector::GetKey(Current).Get(FString()));
}

void SKzTokenTextEditor::OnNamespaceCommitted(const FText& NewText, ETextCommit::Type /*CommitType*/)
{
	FText Current;
	TextHandle->GetValue(Current);
	if (Current.IsCultureInvariant()) { return; }

	// Only apply an actual change, keeping the key stable where possible (stock behavior).
	const FString ProposedNamespace = NewText.ToString();
	const FString CurrentFullNamespace = FTextInspector::GetNamespace(Current).Get(FString());
	if (TextNamespaceUtil::StripPackageNamespace(CurrentFullNamespace).Equals(ProposedNamespace, ESearchCase::CaseSensitive)) { return; }

	const FString* Source = FTextInspector::GetSourceString(Current);
	FString NewNamespace;
	FString NewKey;
	KzTokenTextEditorInternal::GetStableTextId(TextHandle, IEditableTextProperty::ETextPropertyEditAction::EditedNamespace, Source ? *Source : FString(), ProposedNamespace, FTextInspector::GetKey(Current).Get(FString()), NewNamespace, NewKey);
	TextHandle->SetValue(FText::ChangeKey(NewNamespace, NewKey, Current));
}

void SKzTokenTextEditor::OnKeyCommitted(const FText& NewText, ETextCommit::Type /*CommitType*/)
{
	FText Current;
	TextHandle->GetValue(Current);
	if (Current.IsCultureInvariant()) { return; }

	const FString ProposedKey = NewText.ToString();
	if (FTextInspector::GetKey(Current).Get(FString()).Equals(ProposedKey, ESearchCase::CaseSensitive)) { return; }

	const FString* Source = FTextInspector::GetSourceString(Current);
	FString NewNamespace;
	FString NewKey;
	KzTokenTextEditorInternal::GetStableTextId(TextHandle, IEditableTextProperty::ETextPropertyEditAction::EditedKey, Source ? *Source : FString(), FTextInspector::GetNamespace(Current).Get(FString()), ProposedKey, NewNamespace, NewKey);
	TextHandle->SetValue(FText::ChangeKey(NewNamespace, NewKey, Current));
}

void SKzTokenTextEditor::OnTextChangedInternal(const FText& NewText)
{
	const FString New = NewText.ToString();
	const FString Old = TextSnapshot;
	TextSnapshot = New;

	if (AutocompleteBraceIndex == INDEX_NONE)
	{
		// A session opens on a single typed "{".
		if (New.Len() == Old.Len() + 1)
		{
			int32 DiffIndex = 0;
			while (DiffIndex < Old.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
			if (New[DiffIndex] == TEXT('{'))
			{
				AutocompleteBraceIndex = DiffIndex;
				AutocompleteFragmentLen = 0;
				if (TokenMenuAnchor.IsValid())
				{
					TokenMenuAnchor->SetIsOpen(true, /*bFocusMenu=*/false);
				}
				if (AutocompletePicker.IsValid())
				{
					AutocompletePicker->SetFilter(FString());
				}
			}
		}
		return;
	}

	// Session active: only single-character edits inside the fragment keep it alive; anything
	// unusual (paste, selection edits, typing elsewhere, closing the brace by hand) ends it.
	const int32 FragmentEnd = AutocompleteBraceIndex + 1 + AutocompleteFragmentLen;
	if (New.Len() == Old.Len() + 1)
	{
		int32 DiffIndex = 0;
		while (DiffIndex < Old.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
		const TCHAR Typed = New[DiffIndex];
		const bool bInFragment = DiffIndex > AutocompleteBraceIndex && DiffIndex <= FragmentEnd;
		const bool bTokenChar = FChar::IsAlnum(Typed) || Typed == TEXT('_') || Typed == TEXT(':') || Typed == TEXT('-');
		if (!bInFragment || !bTokenChar)
		{
			CancelTokenAutocomplete();
			return;
		}
		++AutocompleteFragmentLen;
	}
	else if (New.Len() == Old.Len() - 1)
	{
		int32 DiffIndex = 0;
		while (DiffIndex < New.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
		const bool bInFragment = DiffIndex > AutocompleteBraceIndex && DiffIndex <= FragmentEnd && AutocompleteFragmentLen > 0;
		if (!bInFragment)
		{
			CancelTokenAutocomplete();
			return;
		}
		--AutocompleteFragmentLen;
	}
	else
	{
		CancelTokenAutocomplete();
		return;
	}

	if (AutocompletePicker.IsValid() && New.IsValidIndex(AutocompleteBraceIndex) && New[AutocompleteBraceIndex] == TEXT('{'))
	{
		AutocompletePicker->SetFilter(New.Mid(AutocompleteBraceIndex + 1, AutocompleteFragmentLen));
	}
	else
	{
		CancelTokenAutocomplete();
	}
}

FReply SKzTokenTextEditor::OnKeyDownInternal(const FKeyEvent& KeyEvent)
{
	if (AutocompleteBraceIndex == INDEX_NONE) { return FReply::Unhandled(); }

	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Down || Key == EKeys::Up)
	{
		if (AutocompletePicker.IsValid()) { AutocompletePicker->MoveSelection(Key == EKeys::Down ? 1 : -1); }
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::Left)
	{
		// Expand/collapse the selected base to reach its parts without the mouse; the caret
		// stays put during the session (Esc first to move it).
		if (AutocompletePicker.IsValid()) { AutocompletePicker->SetSelectionExpanded(Key == EKeys::Right); }
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Tab)
	{
		if (AutocompletePicker.IsValid() && AutocompletePicker->AcceptSelection())
		{
			return FReply::Handled();
		}
		// No match to accept: end the session and let the key act normally.
		CancelTokenAutocomplete();
		return FReply::Unhandled();
	}
	if (Key == EKeys::Escape)
	{
		CancelTokenAutocomplete();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SKzTokenTextEditor::AcceptTokenAutocomplete(const FString& TokenText)
{
	if (AutocompleteBraceIndex == INDEX_NONE || !TextEditor.IsValid())
	{
		CancelTokenAutocomplete();
		return;
	}

	const FString Full = TextEditor->GetText().ToString();
	if (!Full.IsValidIndex(AutocompleteBraceIndex) || Full[AutocompleteBraceIndex] != TEXT('{'))
	{
		CancelTokenAutocomplete();
		return;
	}

	// Replace "{fragment" with the full token, put the caret right after it, commit.
	const FString Result = Full.Left(AutocompleteBraceIndex) + TokenText + Full.Mid(AutocompleteBraceIndex + 1 + AutocompleteFragmentLen);
	const int32 CaretOffset = AutocompleteBraceIndex + TokenText.Len();
	CancelTokenAutocomplete();

	TextSnapshot = Result;
	TextEditor->SetText(FText::FromString(Result));

	int32 LineIndex = 0;
	int32 LineStart = 0;
	for (int32 i = 0; i < CaretOffset && i < Result.Len(); ++i)
	{
		if (Result[i] == TEXT('\n'))
		{
			++LineIndex;
			LineStart = i + 1;
		}
	}
	TextEditor->GoTo(FTextLocation(LineIndex, CaretOffset - LineStart));
	FSlateApplication::Get().SetKeyboardFocus(TextEditor);

	TextHandle->SetValue(FText::FromString(Result));
}

void SKzTokenTextEditor::CancelTokenAutocomplete()
{
	AutocompleteBraceIndex = INDEX_NONE;
	AutocompleteFragmentLen = 0;
	if (TokenMenuAnchor.IsValid() && TokenMenuAnchor->IsOpen())
	{
		TokenMenuAnchor->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE