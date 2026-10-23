<p align="center">
  <img src="https://kirzo.dev/content/images/plugins/KzDialogue_banner.jpg" alt="KzDialogue Banner" width="512">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Unreal%20Engine-5.7-blue?logo=unrealengine" alt="Unreal Engine 5.7" />
  <img src="https://img.shields.io/badge/language-C%2B%2B20-00599C?logo=c%2B%2B" alt="C++20" />
  <img src="https://img.shields.io/badge/Blueprint-ready-orange" alt="Blueprint ready" />
  <img src="https://img.shields.io/badge/license-MIT-green.svg" alt="MIT License" />
  <img src="https://img.shields.io/github/stars/kirzo/KzDialogue?style=social" alt="GitHub stars" />
</p>

**KzDialogue** is a data-driven dialogue and subtitles system for **Unreal Engine 5.7**. Lines, audio, subtitles, cinematics and gameplay events all come from the same authored asset, and the runtime keeps data, playback and presentation apart: any number of views (subtitles, a history log, world-space bubbles) can follow the same player.

It comes with a dialogue asset editor with a notify timeline per line, a Sequencer track, Blueprint nodes with inline line pickers, named tokens with gender agreement, a complete localization toolchain (stable keys, CSV and PO round-trips, coverage, a project dashboard, validators) and console commands for debugging. It ships no content: the subtitle widget is a base class you style yourself.

---

## ⚠️ Requirements

* **Unreal Engine 5.7.**
* **[KzLib](https://github.com/kirzo/KzLib)**: shared utilities, editor widgets and the asset validation base.

## 📦 Installation

1. Create a `Plugins` folder in your project root if there is none.
2. Clone or download this repository and KzLib into it:

   ```
   YourProject/
   └── Plugins/
       ├── KzDialogue/
       └── KzLib/
   ```
3. Regenerate the project files and build, or open the `.uproject` and let the editor build the modules.
4. Check that both plugins are enabled in **Edit > Plugins**.
5. C++ projects only: add `"KzDialogue"` (and `"GameplayTags"` if you use channel tags in code) to your module's `PublicDependencyModuleNames`.

---

## 🚀 Quick Start

From an empty project to a subtitled line on screen, all in Blueprint.

### 1. Check the channels

**Project Settings > Plugins > KzDialogue** already declares three channels:

| Channel | Default priority | Interruptible |
|---|---|---|
| `Dialogue.Channel.Main` | 100 | Yes |
| `Dialogue.Channel.Bark` | 10 | Yes |
| `Dialogue.Channel.System` | 500 | No |

`Dialogue.Channel.Main` is the default channel. Each channel has its own player, so a bark and a conversation can run at the same time. A child tag such as `Dialogue.Channel.Bark.Guard` is a channel of its own that uses its parent's definition.

### 2. Create a speaker

In the Content Browser: **Add > KzLib > Dialogues > Dialogue Speaker**. Fill in **Display Name**, or the structured **Given Name**, **Family Name** and **Honorific** parts. Set a **Token** such as `Kirzo` if lines should be able to name this character as `{Kirzo}`.

### 3. Create a dialogue

**Add > KzLib > Dialogues > Dialogue**. Open it and add lines in the **Lines** tab: pick the **Speaker**, write the **Text**, and set an **Audio** asset if the line is voiced. A line without audio lasts its **Duration**, or 2.5 seconds by default.

### 4. Create the subtitle widget

1. Create a Widget Blueprint with **Kz Subtitle Widget** as its parent class.
2. Add two **Text Block**s, one for the speaker name and one for the text. Animations are optional: one for the dialogue entering, one for it leaving, and one in and one out for each line.
3. Override **Get View For Channel**. Return a **Kz Subtitle Channel View** holding your text blocks and animations for the channels this widget shows, and an empty one for the rest.
4. In **Class Defaults**, set **Listened Channels** to `Dialogue.Channel` (every channel) or to the ones this widget shows.
5. Create the widget and **Add to Viewport**, for example from your HUD or player controller's **BeginPlay**.

A view without animations makes the player skip those phases. When a view has animations, the player waits for them, so never hide the widget with **Collapsed** visibility mid-dialogue; use **Set View Enabled** instead, for example for a "subtitles off" option.

### 5. Play it

Call **Play Dialogue Asset** from any Blueprint with your asset. Leave the channel empty to use the asset's own, then the project default.

* **Play Dialogue Asset (Async)** waits for the dialogue and fires **Finished** or **Cancelled**.
* **Play Dialogue Line** plays a single line, picked from a dropdown on the node.

From C++:

```cpp
#include "KzDialogueFunctionLibrary.h"

UKzDialogueFunctionLibrary::PlayDialogueAsset(this, DialogueAsset, FGameplayTag());
```

### 6. Give the speaker a body (optional)

Add a **Kz Dialogue Speaker** component to the character and set its **Speaker** to the same speaker asset. Line audio then plays attached to that actor, the default spatialization, and the component exposes a speaking level (`GetSpeakingLevel`, `OnSpeakingLevelChanged`) to drive mouth animation.

---

## 🌟 Core Concepts

### 🗂️ Dialogue assets and lines

`UKzDialogueAsset` holds an ordered list of lines. Every line has a stable `LineId`, kept across reorders and edits, so Sequencer sections, Blueprint nodes and save data keep pointing at it. A line carries:

* **Speaker** and **Text**. The text is a localizable `FText` and may use tokens (see below).
* **Audio**, with a start delay and an optional in/out range inside the wave.
* **Duration** and **Duration Mode**: follow the audio (`Auto`), a fixed time (`Override`), or the audio plus extra time (`ExtendAudio`).
* **Tags**, merged with the asset's own tags, which views can filter on (`LineHasTag`).
* An optional per-line channel, translator notes, a character limit and a notify timeline.

**Aliases** are named groups of interchangeable lines. Each play picks one by the alias's **Selection Mode** (`Random`, `RandomNoRepeat`, `ShuffleBag`, `Sequential`) and honours its **Cooldown**. Anything that plays a line can play an alias.

**Line refs** (`FKzDialogueLineRef`: an asset plus a line or alias) and **line lists** (`FKzDialogueLineList`: an asset plus several ids) are the Blueprint-friendly way to point at content. **Make Dialogue Line Ref** and **Make Dialogue Line List** have pickers on the node.

A dialogue advances on its own by default. Set **Advance Mode** to `Manual` on the asset or the play call to hold each line until `Next()` is called on the player, as in an RPG.

### 🎚️ Channels, players and sessions

`UKzDialogueSubsystem` (a world subsystem) owns one `UKzDialoguePlayer` per channel and arbitrates requests by priority. A request with an equal or higher priority takes the channel, unless the channel or the playing dialogue refuses interruption. The priority comes from the call, then the asset, then the channel definition.

A line may pick its own channel, explicitly or through a **Sound Class Channels** mapping in the project settings. When an asset's lines land on different channels, a `UKzDialogueAssetSession` plays them as consecutive runs and reports one finish for the whole asset.

The player is a state machine (`Idle`, `Entering`, `LineEntering`, `LinePlaying`, `LineExiting`, `Exiting`, `Paused`):

* **Commands**: `Pause`, `Resume`, `Next`, `Stop` (graceful, with exit animations), `Interrupt` (immediate, short audio fade) and `Abort` (immediate, no fades).
* **Events**: `OnDialogueStarted`, `OnDialogueFinished` (with the finish reason), `OnLineStarted`, `OnLineFinished`, `OnPaused` and `OnResumed`, plus `BindOnSpecificLineStarted` / `BindOnSpecificLineFinished` for one line or alias.

### 🗣️ Speakers, words and named tokens

* **Speaker assets** (`UKzSpeakerAsset`) hold a character's localized name: a plain display name, or structured parts composed by a per-culture format (`{Family} {Given}{Honorific}` for Japanese, for example), plus a gender. A speaker component can wear extra personas, for a character whose name is not known yet.
* **Word assets** (`UKzWordAsset`) are shared vocabulary with gender forms, such as honorifics or titles.
* **Named tokens** let any text name them: `{Kirzo}` renders the default name, `{Kirzo:given}` or `{Kirzo:nick}` a specific part. In a translation, `{Kirzo:gender}` feeds the ICU `|gender(...)` syntax so the sentence agrees with the character.
* **Runtime overrides** live in `UKzNamedTokenSubsystem` (game instance, survives level travel): **Set Named Token Part** and **Set Named Token Gender** for a player-chosen name, with a snapshot for save games.
* **Other arguments**: **Register Text Argument Resolver** fills `{PlayerName}`-style arguments for every line, and **Set Line Text Argument** / **Set Line Number Argument** fill them for one play.
* **`FKzTokenText`** gives the same token editor to any text outside dialogue, such as UI or item names. Resolve it with **Resolve Token Text**.

#### Gender agreement

A player-chosen character's gender is unknown when the text is translated, so the translation carries every form and the token picks one:

* Source: `{player} is exhausted.`
* Spanish: `{player} está {player:gender}|gender(agotado, agotada, agotade).`

`{player}` prints the name. `{player:gender}` prints nothing: it is the speaker asset's gender, or its runtime override. `|gender(masculine, feminine, neuter)` prints the form that agrees; the third one covers Unspecified and works for articles and pronouns too. English needs none of it: it lives only in translations.

### 💬 Subtitles

`UKzSubtitleWidget` renders any number of channels from one widget, each with its own view.

* **Mute Rules** hide some channels while another plays, for example barks while the main conversation runs.
* **Speaker Formatting Rules** add affixes such as `Kirzo:`, per culture if needed.
* **Can Show Line** filters lines, for example by tag.
* Right-to-left cultures mirror the layout automatically.

To build a view from scratch, bind to the player's `OnRequest*` events, call `ClaimViewResponse()` when you need time, and answer with the matching `Notify*Finished()`.

### ⏱️ Notify timelines

Each line can carry a timeline of notifies, edited in the line's details with audio preview and the waveform. Events are timed in seconds, as a fraction of the line, or anchored to a cue point in the wave, so they follow each language's recording.

* **Point notifies** fire once.
* **State notifies** pair a begin and an end even when the line is cut short.
* **Built-in notifies**: Play Montage, Play Animation, Play Sound, Camera Shake, Force Feedback and Set Tag, with state versions of the montage and the animation.
* **Your own**: make a Blueprint child of **Kz Dialogue Notify** (`Notify`) or **Kz Dialogue Notify State** (`NotifyBegin`, `NotifyTick`, `NotifyEnd`).

### 📢 Barks

Add a **Kz Bark** component to an actor and point its **Bark** at a line or, better, an alias. It barks every **Min Interval** to **Max Interval** seconds on its channel, and keeps going after a bark is interrupted. Control it with `StartBarking`, `StopBarking`, `PauseBarking`, `ResumeBarking` and `BarkNow`.

### 🎞️ Sequencer

1. In a Level Sequence, **+ Track > Dialogue Track**.
2. Set the track's **Dialogue Asset** and **Channel** in the details panel.
3. Use **+ Section** on the track to pick a line from a searchable list.
4. The section's length is the line's time on screen. **Suppress Audio** leaves the audio to the sequence's own tracks.

### 🌍 Localization

Every text gets a stable key derived from its asset and line, so translations survive reordering, renaming and duplication. Setup:

1. In the **Localization Dashboard**, create a target named `Game` (or set **Localization Target Name** in the plugin settings) with **Gather from Packages** over your content and the cultures you need.
2. Run **Gather Text**.

The tools then work from the asset editor's **Localization** tab, the project-wide **Dialogue Dashboard** (in the **Tools** menu) and the dialogue assets' Content Browser context menu:

* **Coverage** per culture: text, localized audio and pending words, with each line's state.
* **Round-trips**:
  * CSV and PO export and import, with translator context (speaker, notes, character limit, audio).
  * Drift detection: rows whose source changed after the export are skipped.
  * Source fixes: corrections to the original text made in the spreadsheet are imported back.
* **Inline editing**: translations written in place.
* **Identical texts**: merge them into one translation, or mark them non-localizable.
* **Pseudo-localization**, for overflow and font testing.
* **Voice-over tracking**: a line whose text changed after its take was recorded is flagged.
* **Localized audio** uses the engine's `L10N/<culture>` folders. **Localize Audio** opts a line out.

### ✅ Validation

The plugin's validators run when you save, through **Validate Assets** and in CI. They check:

* Lines: empty lines, broken audio, audio ranges outside the wave, duplicate ids, and takes recorded against an older text.
* Aliases: empty, with missing lines, with mismatched speakers, or with duplicate names.
* Timelines: events without a notify or a time, point notifies past the line's end, and notifies missing their montage, sound or other setting.
* Localization: texts off their stable key, stale translations, character limits, placeholder mismatches, and unknown tokens or parts.
* Named assets: tokens claimed twice, and asset ids shared by files copied outside the editor.

### 🐞 Debugging

* `Kz.Dialogue.Debug 1` shows an overlay with every channel's state.
* Console commands: `Kz.Dialogue.Play <AssetPath> [Channel]`, `Kz.Dialogue.Stop`, `Kz.Dialogue.Next`, `Kz.Dialogue.Pause`, `Kz.Dialogue.Resume`, `Kz.Dialogue.Abort`, `Kz.Dialogue.Interrupt`, `Kz.Dialogue.SetSpeed`, `Kz.Dialogue.ListChannels`, `Kz.Dialogue.ListSpeakers`, `Kz.Dialogue.Status` and `Kz.Dialogue.ResetAliasStates`.

---

## 🧩 Extension Points

* **Views**: subclass `UKzSubtitleWidget`, or write any object that binds to a player's events (see Subtitles).
* **Providers**: subclass `UKzDialogueProvider` (`HasNext`, `Advance`) to feed lines from a branching graph or generated content, and play it with the subsystem's `Play`.
* **Notifies**: Blueprint or C++ children of `UKzDialogueNotify` and `UKzDialogueNotifyState`.
* **Text arguments**: resolvers registered on `UKzNamedTokenSubsystem`.

## 📦 Cooking

* **Speaker and word assets always cook.** The plugin's `Config/Game.ini` adds an Asset Manager rule for them, because tokens name them in text without any package reference.
* **Dialogue assets cook when something references them**, such as a Blueprint, a level or a sequence. If you only play them by path, add a `PrimaryAssetTypesToScan` rule for the `KzDialogue` type.

---

## License

Released under the [MIT License](LICENSE).