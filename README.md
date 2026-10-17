# LEGO Racers — UE5

A recreation of LEGO Racers (1999) in Unreal Engine 5: the original game's races, tracks, menus, builder and
handling as you remember them, running on a modern engine.

> **Alpha: experimental.** The whole game is playable, but expect bugs, crashes and visual glitches, and saves
> that may not carry over between versions. The Modern look is still changing. Tested so far: Linux on a desktop
> PC; the Windows and AYN Thor builds haven't been played on real hardware yet. Reports are welcome in
> [Issues](../../issues).

Two looks, switched with **F9** in game:

- **Classic**: the original's pixels and lighting.
- **Modern**: HD textures, golden-hour lighting and relief on the tracks.

## You need your own copy of the game

No original game files come with these builds. The music and HD textures are included but locked: they open only
with the `LEGO.JAM` file from your own LEGO Racers install.

1. Download the build for your platform from the table below (always the latest), or any version from [Releases](../../releases).
2. Extract it and run the game (see `README.txt` in the download).
3. On first launch, choose `LEGO.JAM` from your LEGO Racers folder (next to `LEGORacers.exe`). It's copied into
   `LegoRacers/GameData/`, so you're only asked once. No file dialog on your system (e.g. a handheld's game mode)?
   Copy `LEGO.JAM` into `LegoRacers/GameData/` yourself.

Video settings: OPTIONS > VIDEO OPTIONS, or Esc during a race.

## Platforms

| Platform | Download (latest) | Run |
|---|---|---|
| Windows (x64) | [LegoRacers-Windows.zip](https://github.com/zmelumian972/LRUE5/releases/latest/download/LegoRacers-Windows.zip) | `LegoRacers.exe` |
| Linux (x86-64) | [LegoRacers-Linux.tar.gz](https://github.com/zmelumian972/LRUE5/releases/latest/download/LegoRacers-Linux.tar.gz) | `./LegoRacers.sh` |
| AYN Thor (Linux, ARM64) | [LegoRacers-Thor.tar.gz](https://github.com/zmelumian972/LRUE5/releases/latest/download/LegoRacers-Thor.tar.gz) | `./LegoRacers-Arm64.sh -nosplash` |

On Windows, if it says `VCRUNTIME140.dll` or `MSVCP140.dll` is missing, install Microsoft's
[Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

LEGO and LEGO Racers are trademarks of the LEGO Group. This is a fan project, not affiliated with or endorsed by it.
