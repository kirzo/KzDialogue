// Copyright 2026 kirzo

#include "Debug/KzDialogueDebugOverlay.h"

#if !UE_BUILD_SHIPPING

#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueSpeakerComponent.h"
#include "KzSpeakerAsset.h"

#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"
#include "Sound/SoundBase.h"

namespace KzDialogueDebugDraw
{
	// --- Layout constants ----------------------------------------------------------------

	constexpr float OriginX = 10.f;
	constexpr float OriginY = 60.f;   // leave room for "stat fps" if it's on
	constexpr float LineHeight = 14.f;
	constexpr float SectionGap = 6.f;
	constexpr float Indent = 16.f;

	// --- Color palette --------------------------------------------------------------------

	const FLinearColor TitleColor(0.55f, 0.78f, 1.00f, 1.f);  // header
	const FLinearColor LabelColor(0.85f, 0.85f, 0.85f, 1.f);  // field label
	const FLinearColor ValueColor(1.00f, 1.00f, 1.00f, 1.f);  // value text
	const FLinearColor MutedColor(0.70f, 0.70f, 0.70f, 1.f);  // secondary info
	const FLinearColor ActiveColor(0.40f, 0.95f, 0.45f, 1.f);  // active state
	const FLinearColor IdleColor(0.55f, 0.55f, 0.55f, 1.f);  // idle state
	const FLinearColor WarningColor(1.00f, 0.75f, 0.30f, 1.f);

	static FLinearColor StateColor(EKzDialogueState State)
	{
		switch (State)
		{
		case EKzDialogueState::Idle:    return IdleColor;
		case EKzDialogueState::Paused:  return WarningColor;
		default:                        return ActiveColor;
		}
	}

	static FString StateName(EKzDialogueState State)
	{
		const UEnum* Enum = StaticEnum<EKzDialogueState>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(State)) : TEXT("?");
	}

	static FString ProviderTypeName(const UKzDialogueProvider* Provider)
	{
		if (!IsValid(Provider)) { return TEXT("none"); }
		if (const UKzAssetDialogueProvider* Asset = Cast<UKzAssetDialogueProvider>(Provider))
		{
			const FString AssetName = IsValid(Asset->Asset) ? Asset->Asset->GetName() : TEXT("<null>");
			return FString::Printf(TEXT("AssetProvider (%s)"), *AssetName);
		}
		if (Provider->IsA<UKzManualDialogueProvider>())
		{
			return TEXT("ManualProvider");
		}
		return Provider->GetClass()->GetName();
	}

	/** Draw a single line of text and advance Y. */
	static void DrawLine(UCanvas* Canvas, float X, float& Y, const FString& Text, const FLinearColor& Color)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), GEngine->GetSmallFont(), Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
		Y += LineHeight;
	}

	/** Draw "Label: Value" with two distinct colors. */
	static void DrawLabeled(UCanvas* Canvas, float X, float& Y, const FString& Label, const FString& Value)
	{
		FCanvasTextItem LabelItem(FVector2D(X, Y), FText::FromString(Label), GEngine->GetSmallFont(), LabelColor);
		LabelItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(LabelItem);

		float LabelW = 0.f, LabelH = 0.f;
		Canvas->StrLen(GEngine->GetSmallFont(), Label, LabelW, LabelH);

		FCanvasTextItem ValueItem(FVector2D(X + LabelW + 4.f, Y), FText::FromString(Value), GEngine->GetSmallFont(), ValueColor);
		ValueItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(ValueItem);

		Y += LineHeight;
	}
}

// =======================================================================================
// Construction / registration
// =======================================================================================

FKzDialogueDebugOverlay::FKzDialogueDebugOverlay()
{
	CVarRef = MakeUnique<FAutoConsoleVariableRef>(
		TEXT("Kz.Dialogue.Debug"),
		bDebugEnabled,
		TEXT("Show the KzDialogue debug overlay. 0 = off, 1 = on."),
		ECVF_Cheat);

	DrawHandle = UDebugDrawService::Register(TEXT("Game"),
		FDebugDrawDelegate::CreateRaw(this, &FKzDialogueDebugOverlay::DrawDebug));
}

FKzDialogueDebugOverlay::~FKzDialogueDebugOverlay()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
	CVarRef.Reset();
}

UWorld* FKzDialogueDebugOverlay::ResolveActiveWorld() const
{
	if (!GEngine) { return nullptr; }

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && Context.WorldType == EWorldType::PIE) { return Context.World(); }
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && Context.WorldType == EWorldType::Game) { return Context.World(); }
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World()) { return Context.World(); }
	}
	return nullptr;
}

// =======================================================================================
// Drawing
// =======================================================================================

void FKzDialogueDebugOverlay::DrawDebug(UCanvas* Canvas, APlayerController* /*PC*/)
{
	if (bDebugEnabled == 0 || !Canvas) { return; }

	using namespace KzDialogueDebugDraw;

	UWorld* World = ResolveActiveWorld();
	if (!World) { return; }

	UKzDialogueSubsystem* Sub = World->GetSubsystem<UKzDialogueSubsystem>();
	if (!Sub) { return; }

	float X = OriginX;
	float Y = OriginY;

	// --- Header ----------------------------------------------------------------------
	DrawLine(Canvas, X, Y, TEXT("=== KzDialogue Debug ==="), TitleColor);
	Y += SectionGap * 0.5f;

	// --- Channels --------------------------------------------------------------------
	TArray<UKzDialoguePlayer*> Players;
	Sub->GetAllPlayers(Players);

	if (Players.Num() == 0)
	{
		DrawLine(Canvas, X, Y, TEXT("(no active channels)"), MutedColor);
	}

	for (const UKzDialoguePlayer* Player : Players)
	{
		if (!IsValid(Player)) { continue; }

		const FString ChannelHeader = FString::Printf(TEXT("Channel: %s"), *Player->Channel.ToString());
		DrawLine(Canvas, X, Y, ChannelHeader, TitleColor);

		// State + priority + scale
		const FString State = StateName(Player->GetState());
		const FString StateLine = FString::Printf(
			TEXT("State: %s   Priority: %d   TimeScale: %.2fx"),
			*State, Player->CurrentPriority, Player->TimeScale);

		FCanvasTextItem StateItem(FVector2D(X + Indent, Y), FText::FromString(StateLine),
			GEngine->GetSmallFont(), StateColor(Player->GetState()));
		StateItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(StateItem);
		Y += LineHeight;

		// Provider
		DrawLabeled(Canvas, X + Indent, Y, TEXT("Provider:"), ProviderTypeName(Player->GetProvider()));

		// Current line (if any)
		if (Player->IsPlaying())
		{
			const FKzDialogueLine& Line = Player->GetCurrentLine();
			const FString LineDesc = FString::Printf(
				TEXT("(%s) \"%s\""),
				*Line.Speaker.GetDisplayLabel().ToString(),
				*Line.Text.ToString());

			DrawLabeled(Canvas, X + Indent, Y, TEXT("Line:"), LineDesc);

			// Audio (if any)
			if (USoundBase* Sound = Line.Audio.Get())
			{
				DrawLabeled(Canvas, X + Indent, Y, TEXT("Audio:"), Sound->GetName());
			}
			else if (!Line.Audio.IsNull())
			{
				DrawLabeled(Canvas, X + Indent, Y, TEXT("Audio:"), TEXT("(unloaded)"));
			}
		}

		Y += SectionGap;
	}

	// --- Speakers --------------------------------------------------------------------
	TArray<UKzDialogueSpeakerComponent*> Speakers;
	for (TObjectIterator<UKzDialogueSpeakerComponent> It; It; ++It)
	{
		if (It->GetWorld() == World) { Speakers.Add(*It); }
	}

	const FString SpeakerHeader = FString::Printf(TEXT("=== Speakers (%d) ==="), Speakers.Num());
	DrawLine(Canvas, X, Y, SpeakerHeader, TitleColor);
	Y += SectionGap * 0.5f;

	if (Speakers.Num() == 0)
	{
		DrawLine(Canvas, X + Indent, Y, TEXT("(none registered)"), MutedColor);
	}

	for (const UKzDialogueSpeakerComponent* Speaker : Speakers)
	{
		if (!IsValid(Speaker)) { continue; }

		const FString OwnerName = Speaker->GetOwner() ? Speaker->GetOwner()->GetName() : TEXT("<no owner>");
		const FString SpeakerLine = FString::Printf(
			TEXT("%s   on %s"),
			Speaker->Speaker ? *Speaker->Speaker->GetName() : TEXT("<none>"),
			*OwnerName);

		DrawLine(Canvas, X + Indent, Y, SpeakerLine, MutedColor);
	}
}

#endif // !UE_BUILD_SHIPPING