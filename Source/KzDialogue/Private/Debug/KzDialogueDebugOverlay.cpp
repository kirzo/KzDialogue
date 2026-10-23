// Copyright 2026 kirzo

#include "Debug/KzDialogueDebugOverlay.h"

#if !UE_BUILD_SHIPPING

#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAssetSession.h"
#include "KzDialogueNotify.h"
#include "KzDialogueSpeakerComponent.h"
#include "KzSpeakerAsset.h"

#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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
		return IsValid(Provider) ? Provider->GetClass()->GetName() : TEXT("none");
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

		DrawLine(Canvas, X, Y, FString::Printf(TEXT("Channel: %s"), *Player->Channel.ToString()), TitleColor);

		// The phase stays visible under a pause.
		const FString StateLine = FString::Printf(TEXT("State: %s%s   Priority: %d   TimeScale: %.2fx   Pending acks: %d"),
			*StateName(Player->State), Player->IsPaused() ? TEXT(" (paused)") : TEXT(""), Player->CurrentPriority, Player->TimeScale, Player->PendingViewAcks);
		DrawLine(Canvas, X + Indent, Y, StateLine, StateColor(Player->GetState()));

		FString Source = ProviderTypeName(Player->GetProvider());
		for (const UKzDialogueAssetSession* Session : Sub->AssetSessions)
		{
			if (Session && Session->GetCurrentPlayer() == Player)
			{
				Source = FString::Printf(TEXT("%s (asset session)"), *GetNameSafe(Session->GetAsset()));
				break;
			}
		}
		DrawLabeled(Canvas, X + Indent, Y, TEXT("Source:"), Source);

		if (Player->IsPlaying())
		{
			const FKzDialogueLine& Line = Player->CurrentLine;
			const FString LineDesc = FString::Printf(TEXT("(%s) \"%s\""), *Line.Speaker.GetDisplayLabel(Player).ToString(), *Line.GetFormattedText().ToString());
			DrawLabeled(Canvas, X + Indent, Y, TEXT("Line:"), LineDesc);

			if (USoundBase* Sound = Line.Audio.Get())
			{
				DrawLabeled(Canvas, X + Indent, Y, TEXT("Audio:"), Sound->GetName());
			}
			else if (!Line.Audio.IsNull())
			{
				DrawLabeled(Canvas, X + Indent, Y, TEXT("Audio:"), TEXT("(unloaded)"));
			}

			if (Player->ActiveNotifies.Num() > 0)
			{
				DrawLabeled(Canvas, X + Indent, Y, TEXT("Timeline:"), FString::Printf(TEXT("%.2f / %.2f s"), Player->TimelineClock, Player->CachedLineDuration));

				TArray<FString> ActiveStates;
				for (const FKzDialogueActiveNotify& Entry : Player->ActiveNotifies)
				{
					if (Entry.bActive && IsValid(Entry.Notify)) { ActiveStates.Add(Entry.Notify->GetNotifyName().ToString()); }
				}
				if (ActiveStates.Num() > 0)
				{
					DrawLabeled(Canvas, X + Indent, Y, TEXT("States:"), FString::Join(ActiveStates, TEXT(", ")));
				}
			}
		}

		Y += SectionGap;
	}

	// --- Speakers --------------------------------------------------------------------
	TMap<const UKzSpeakerAsset*, UKzDialogueSpeakerComponent*> Speakers;
	UKzDialogueSpeakerComponent::GetRegisteredSpeakers(World, Speakers);

	DrawLine(Canvas, X, Y, FString::Printf(TEXT("=== Speakers (%d) ==="), Speakers.Num()), TitleColor);
	Y += SectionGap * 0.5f;

	if (Speakers.Num() == 0)
	{
		DrawLine(Canvas, X + Indent, Y, TEXT("(none registered)"), MutedColor);
	}

	for (const TPair<const UKzSpeakerAsset*, UKzDialogueSpeakerComponent*>& Pair : Speakers)
	{
		const FString SpeakerLine = FString::Printf(TEXT("%s%s   on %s"), *Pair.Key->GetName(), Pair.Value->Speaker != Pair.Key ? TEXT(" (persona)") : TEXT(""), *GetNameSafe(Pair.Value->GetOwner()));
		DrawLine(Canvas, X + Indent, Y, SpeakerLine, MutedColor);
	}
}

#endif // !UE_BUILD_SHIPPING