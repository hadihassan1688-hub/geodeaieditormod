# AI Level Builder (Geode mod)

A Geometry Dash mod for [Geode](https://geode-sdk.org) 5.9 / GD 2.2081 that builds levels for you from inside the editor.

Open any level in the editor and press the pink **AI** button next to undo / redo / delete.

## What it does

**Generate (works offline, no account)**

Pick a style, a difficulty and a length, then press *Generate*. Words typed in the box count too:

```
long hell wave level, extreme, seed 42
easy 30s ship level
```

| Style | What you get |
|---|---|
| Classic | mostly cube, bright colours |
| Hell Demon | dark red and black, fast, tight wave and ship parts, sawblades, pulses |
| Neon Mix | every game mode, neon colours |
| Wave Rush | almost all wave |
| Sky Flight | ship, ufo and swing |

The generator builds cube and robot jumps (spikes, stairs, orb gaps), ship and swing corridors, ufo gates, wave zig-zags and ball / spider gravity runs, and puts the mode and speed portals between them. Difficulty goes from 1 (Auto) to 10 (Extreme Demon) and changes gap sizes, spike counts, speeds and spacing.

**Ask AI (needs a Claude API key)**

Type anything and press *Ask AI*. The request goes to Claude together with a description of the level, and the answer is applied to the editor. It can:

- build a whole level or add a part ("add a 10 second slow ship part at the end")
- place individual objects, decoration, text and triggers
- move, change or delete existing objects ("delete everything after x=200", "move the part between x=50 and x=80 up 2 blocks")
- change colours ("make it look like a hell level")
- answer questions about the level

Set the key in the mod's settings (the gear in the popup). Requests are billed to your Anthropic account. The key is stored in the mod's settings file on your device and is only sent to `api.anthropic.com`.

## Good to know

- Everything that is added goes in as one step, so one undo removes it. **Replace the whole level** asks first, because clearing a level cannot be undone.
- The generator makes gameplay layouts, not decorated levels, and it does not check them by playing them. Playtest and fix what feels unfair, especially orb gaps and difficulty 9 to 10.
- Asking for a level "like" a famous level gives you an original level in that style and difficulty, not a copy of it.
- The AI sees at most 600 existing objects by default (setting: *Objects shown to the AI*). It tells you when it could not see the whole level.

## Building

The easiest way is GitHub: push this folder to a repository and the included workflow (`.github/workflows/build.yml`) builds the `.geode` file for Windows, macOS, iOS and Android. Download it from the run's *Build Output* artifact and drop it into your Geode `mods` folder.

To build on your own machine, install the [Geode SDK and CLI](https://docs.geode-sdk.org/getting-started/), then:

```bash
geode build
```

Before publishing, change `id` and `developer` in `mod.json` to your own.

## Code map

| File | Job |
|---|---|
| `src/LevelBuilder.*` | the generator, plain C++ with no game code |
| `src/AIClient.*` | the Claude request and reading the plan it returns |
| `src/EditorBridge.*` | reading the level and applying changes in the editor |
| `src/AIPopup.*` | the popup |
| `src/main.cpp` | the editor button |
| `tests/gen_test.cpp` | standalone test of the generator |

Run the generator test without the game:

```bash
g++ -std=c++20 -Isrc tests/gen_test.cpp src/LevelBuilder.cpp -o gen_test && ./gen_test
```
