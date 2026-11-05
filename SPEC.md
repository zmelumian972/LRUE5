# LEGO Racers on UE5: project spec

What this project is for and the rules it's built by.

## The goal

**Recreate LEGO Racers (1999) in Unreal Engine 5, as our own code, so that someone with their own copy of the game can
play the whole of it on a modern machine and it feels like the original.**

That has three parts:

- **The whole game.** Everything the original lets you do, not a demo or a single-track slice.
- **It plays the same.** Handling, AI, power-ups, rules and menu flow match the original closely enough that a player
  can't tell. Being close is measured, not judged by eye alone.
- **It's ours.** The code is written from scratch. It contains no game assets or tables: the player's own install
  supplies them.

## What "the whole game" covers

| Area | What has to work |
|---|---|
| Driving | Kart physics at the original's 62.5 Hz fixed step, track collision, surfaces, walls, spins |
| Racing | Grids, AI opponents on their routes with the rubber band, contact between karts, checkpoints, laps, standings |
| Power-ups and hazards | Bricks and white bricks, every action, explosions, their effects on racers, each track's hazards, events and secrets |
| Modes | Circuit, single race, two-player versus, time race with ghosts, test drive, the attract demo |
| Progression | Points, awards, unlocks, records, cheat codes |
| Builders | Car builder, edit car, driver builder, license, and the race numbers a built car produces |
| Menus | Every screen and the flow between them, the original's fonts, sounds and fades, keyboard, mouse and pad |
| Cinematics | The legal screen, circuit intros, awards, win-car scenes, credits |
| The race's picture | Cameras, what gets drawn and when, scenery animation, effects, the HUD |
| The look | The Classic look: the original's materials, textures and lighting |
| Sound | Menu and race sounds, positional race audio, the music |
| Saves | Racers, unlocks, records and settings, in our own format |
| Languages | The languages the game's data holds, except Finnish |

## The player's copy of the game

- The player needs their own LEGO Racers (1999). We ship none of its files, and nothing under `Content/` is made from
  game data.
- On first start the game finds the install, or asks the player to select `LEGO.JAM`, with `LEGORacers.exe` next to it.
  The `.exe` must be the known version, checked by SHA-256.
- The game builds a data cache from the install once, behind a progress screen, and plays from that cache. It doesn't
  change or copy the install.
- The game's tables are read from `LEGO.JAM` and `LEGORacers.exe`, not written into our source. What stays in the source
  is references into the data, keyboard and format facts, and a few rules not stored as tables.

## What we change from the original

Faithful doesn't mean frozen. These differences are deliberate:

- **Modern resolutions and frame rates.** Any window size. Races fill a widescreen window with Hor+ framing, menus stay
  4:3, and the 62.5 Hz game can be drawn smoothly above that rate. A video settings screen in the game's own style
  covers resolution, window mode, vsync, frame cap, quality, render scale and frame smoothing.
- **Modern input.** Gamepads with rumble, and the whole keyboard for rebinding controls.
- **Our own save format.** The original's saves don't need to load.

## Rules of the build

**Architecture: three layers, and UE only at the top.**

| Layer | Where | What it does |
|---|---|---|
| Data | `Source/LRData` | Reads LEGO's file formats and builds the cache. Plain C++ |
| Core | `Source/LRCore` | The whole game: fixed-step simulation, rules, menus, progression, saves, and the show's effects and draw lists. Plain C++20 with no UE headers |
| Presentation | `Source/LegoRacers` | UE reads the core's read-only output each frame and draws and plays it. It holds only what changes pixels or sound |

The core holds every value a rule reads and every value a test compares. Physics is never UE's: UE doesn't decide where
a kart goes.

**Engineering.**

- Build only what the game shows, with no unused paths and no options nobody sets.
- Fast on modern CPUs and GPUs, on x86-64 and ARM64 alike. No x86-only intrinsics.

## How we know it's right

Tests compare the game against an outside reference, never against its own output, with tolerances rather than identical
bytes, so the maths stays ours to reorder and optimize. Those tests need reference data that can't be shared, so they
aren't in this repository. Playing the game by hand is part of the check too, because a reference only proves what it
visited. Where a reference and the original game differ, the original wins.

## Platforms

Linux x86-64, Windows x64, Linux ARM64 for the AYN Thor, and macOS, which requires Apple hardware to build.

## Out of scope for now

- **Replacement art:** HD textures and detailed bricks.
- **New content:** new tracks, modes, online play, or changes to the game's balance.
- **Loading the original's saves.**
