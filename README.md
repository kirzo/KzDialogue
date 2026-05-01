# KzDialogue

**KzDialogue** is a modular, data-driven dialogue and subtitles system for **Unreal Engine 5**. It is built around a clean separation between **data**, **playback engine**, and **presentation**, so that lines, audio, subtitles, cinematics, and gameplay events can all be driven from the same authored asset.

It ships with a custom asset editor, a **Sequencer track** for cinematic dialogue, and an example UMG subtitle widget. The runtime is presentation-agnostic: you can swap or stack views (subtitles, history log, 3D worldspace billboards, accessibility captions) on top of the same player.

---

## ⚠️ Dependencies & Installation

This plugin relies on the following custom module to function:

* **[KzLib](https://github.com/kirzo/KzLib)**: Core math and utility libraries.

### Installation Steps

1. Navigate to your Unreal Engine project's root directory.
2. If it doesn't exist, create a folder named `Plugins`.
3. Clone or download this repository and its dependencies into the `Plugins` folder. Your directory structure should look like this:

   ```
   YourProject/
   └── Plugins/
       ├── KzDialogue/
       └── KzLib/
   ```
4. Right-click your project's `.uproject` file and select **Generate Visual Studio project files** (or your IDE's equivalent).
5. Open your IDE and compile the project, or simply launch the `.uproject` file to let Unreal Engine build the missing modules.
6. Ensure the plugins are enabled in your project via **Edit → Plugins**.

---

## 🌟 Core Systems

### 🗂️ Authoring (`UKzDialogueAsset`)

A `UPrimaryDataAsset` describing a dialogue as an ordered list of lines. Each line carries:

* **Speaker**: a `FGameplayTag` identity plus an optional display name override.
* **Localizable text** (`FText`) and an optional `USoundBase` audio clip.
* **Explicit duration**, with fallback to the audio length and finally to a default.
* **Tags** (`FGameplayTagContainer`) for mood, intent, channel hints, etc.
* **Audio params** (`FInstancedPropertyBag`) forwarded to the spawned `UAudioComponent` (bool, int, float, name, object, sound wave).
* **Actions** (`UKzDialogueAction`, `Instanced`) that fire on line start/end without coupling the core to specific gameplay code.

Each line has a stable `FGuid` ID, automatically generated and preserved across edits and duplications, so external references (Sequencer sections, save data) survive reorders.

### 🎙️ Providers

`UKzDialogueProvider` is the abstract source of lines consumed by the player. Two implementations ship out of the box:

* `UKzAssetDialogueProvider`: linear playback of a `UKzDialogueAsset`, with optional start/end line clamps.
* `UKzManualDialogueProvider`: ad-hoc single line, ideal for system messages and barks.

The contract supports branching (`GetChoices` / `SelectChoice`) for future graph-based providers.

### 🎬 Player (`UKzDialoguePlayer`)

The engine of the system: a non-widget `UObject` with an explicit state machine (`Idle → Entering → LineEntering → LinePlaying → LineExiting → Exiting`), audio management, line timing, and a clean event API.

* **Inversion of control**: views drive their own animations and notify the player when ready (`NotifyEnterFinished`, `NotifyLineEnterFinished`, ...). Views without animations skip those phases automatically.
* **Commands**: `Play`, `Pause`, `Resume`, `Stop`, `Abort`, `Interrupt`, `Skip`.
* **Multicast events**: `OnDialogueStarted/Finished`, `OnLineStarted/Finished`, `OnPaused/Resumed`, plus per-phase view request events.

### 🎚️ Subsystem & Channels (`UKzDialogueSubsystem`)

A `UWorldSubsystem` that owns one player **per channel** (`Dialogue.Main`, `Dialogue.Bark`, `Dialogue.System`, ...) and arbitrates between requests using priority. Lower-priority dialogues are interrupted cleanly by higher-priority ones; equal-priority requests preempt the current one.

```cpp
USub->PlayAsset(MyAsset, ChannelTag);
USub->PlayLine(MyLine, ChannelTag, /*Priority=*/10);
USub->StopChannel(ChannelTag);
```

### 🗣️ Speaker Component (`UKzDialogueSpeakerComponent`)

Attach to any actor that can speak. Provides:

* A `SpeakerTag` and `DisplayName` queried at runtime.
* `Speak(Asset)` / `SpeakLine(Line)` helpers that route through the subsystem on the speaker's default channel.
* A per-world registry so other systems can resolve a speaker actor from its tag.

### 💬 Subtitle Widget (`UKzSubtitleWidget`)

A reference UMG view: binds to a `UKzDialoguePlayer`, renders its events as fade-in/out widget animations, and updates speaker/text labels. Auto-detects whether the player should wait for fade animations based on which `BindWidgetAnim` slots are populated, so simple widgets without animations work out of the box.

### 🎞️ Sequencer Integration

A native-feeling Sequencer track for cinematic dialogue:

* `UMovieSceneKzDialogueTrack`: holds a reference to a `UKzDialogueAsset` and a target channel.
* `UMovieSceneKzDialogueSection`: each section plays one specific line, identified by its stable `FGuid` (so asset edits don't silently break references).
* **Searchable line picker** with filters by speaker, gameplay tags, and an option to hide lines already used in the track.
* **Display name** automatically extends to `Kz Dialogue (AssetName)` when an asset is bound.
* `bSuppressAudio` flag for cinematics that play their own audio track.

---

## 📚 Quick Start

### Play a dialogue from gameplay

```cpp
UKzDialogueFunctionLibrary::PlayDialogueAsset(this, MyDialogueAsset, ChannelTag);
```

### Bark a single line from an NPC

```cpp
UKzDialogueSpeakerComponent* Speaker = ...;
FKzDialogueLine BarkLine;
BarkLine.Text = FText::FromString(TEXT("I heard something."));
Speaker->SpeakLine(BarkLine);
```

### Bind a subtitle widget to a channel

```cpp
UKzSubtitleWidget* Widget = CreateWidget<UKzSubtitleWidget>(PlayerController, SubtitleWidgetClass);
Widget->AddToViewport();
UKzDialoguePlayer* Player = GetWorld()->GetSubsystem<UKzDialogueSubsystem>()->GetOrCreatePlayer(ChannelTag);
Widget->BindPlayer(Player);
```

### Drive dialogue from a Sequencer cinematic

1. Add a **Kz Dialogue** track to your Level Sequence.
2. Set its `DialogueAsset` and `Channel` in the details panel.
3. Click **+ Section** on the track and pick a line from the searchable menu.
4. Resize the section to control on-screen duration.

---

## 🧩 Extension Points

* **Custom views**: subclass `UKzSubtitleWidget` or write a brand-new one — the player's event API doesn't care what the view is. Multiple views can listen to the same player.
* **Custom providers**: subclass `UKzDialogueProvider` for branching graphs, runtime-generated dialogue, or middleware adapters (Wwise/FMOD).
* **Line actions**: subclass `UKzDialogueAction` to attach gameplay side effects to lines (camera focus, VFX, GAS effects, quest progression) without polluting the core.

---

## License

Released under the [MIT License](LICENSE).
