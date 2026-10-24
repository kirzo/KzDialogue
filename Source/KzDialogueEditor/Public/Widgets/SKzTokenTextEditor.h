// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

class IPropertyHandle;
class SKzTokenPicker;
class SMenuAnchor;
class SMultiLineEditableTextBox;

/**
 * Multiline editor for an FText property with the named-token toolset: the "{}" browser
 * button inserting at the caret, the inline "{" autocomplete session, the localization
 * toggle, "{token}" highlighting (unclaimed ones stand out), a preview with the tokens
 * resolved and an optional character counter. The text is UNBOUND on purpose: with a
 * bound attribute, InsertTextAtCursor edits get wiped by the attribute refresh and GetText
 * reads the stale binding; external changes re-sync through the handle's change notify (other
 * views) and the undo client. Hosted by the dialogue line customization; any FText property
 * handle works, in or out of the plugin.
 */
class KZDIALOGUEEDITOR_API SKzTokenTextEditor : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SKzTokenTextEditor)
		: _bManagedLocalizationKeys(false)
		{}
		/** True when the host manages the localization identity itself (dialogue lines re-anchor to stable keys): the loc control is a bare on/off toggle. False offers the stock FText identity editing (namespace and key) in a flyout. */
		SLATE_ARGUMENT(bool, bManagedLocalizationKeys)
		/** Character budget of the displayed text; above 0 shows a live "N / MaxCharacters" counter. */
		SLATE_ATTRIBUTE(int32, MaxCharacters)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<IPropertyHandle> InTextHandle);

	//~ FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override { SyncFromHandle(); }
	virtual void PostRedo(bool bSuccess) override { SyncFromHandle(); }

private:
	/** Shows the property's value when it differs from the editor's. */
	void SyncFromHandle();

	/** Re-resolves the preview from TextSnapshot; on every text change, not per frame (resolving loads named assets). */
	void RefreshPreview();

	/** Inline "{" autocomplete: a typed "{" opens the token list under the editor; the fragment after it filters live; Enter/Tab completes, Esc cancels. Tracked by diffing text changes, so anything unusual (paste, selection edits) just ends the session. */
	void OnTextChangedInternal(const FText& NewText);
	FReply OnKeyDownInternal(const FKeyEvent& KeyEvent);
	void AcceptTokenAutocomplete(const FString& TokenText);
	void CancelTokenAutocomplete();

	/** The globe control: a bare toggle when the host manages keys, a flyout with the stock identity editing otherwise. */
	TSharedRef<SWidget> MakeLocalizationControl();
	bool IsLocalizable() const;
	void SetLocalizable(bool bLocalizable);
	FText GetNamespaceValue() const;
	FText GetKeyValue() const;
	void OnNamespaceCommitted(const FText& NewText, ETextCommit::Type CommitType);
	void OnKeyCommitted(const FText& NewText, ETextCommit::Type CommitType);

	TSharedPtr<IPropertyHandle> TextHandle;
	TSharedPtr<SMultiLineEditableTextBox> TextEditor;
	TSharedPtr<SMenuAnchor> TokenMenuAnchor;
	TSharedPtr<SKzTokenPicker> AutocompletePicker;

	/** Diff baseline for detecting the typed "{" and tracking the fragment. */
	FString TextSnapshot;
	/** Index of the session's "{" in the text; INDEX_NONE = no session. */
	int32 AutocompleteBraceIndex = INDEX_NONE;
	int32 AutocompleteFragmentLen = 0;

	bool bManagedLocalizationKeys = false;

	TAttribute<int32> MaxCharacters;

	/** The text as it displays, named tokens resolved; what the counter measures. */
	FText ResolvedPreview;
	bool bHasTokens = false;
};