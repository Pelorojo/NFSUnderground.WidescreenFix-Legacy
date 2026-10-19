# NFSUnderground.WidescreenFix Legacy (EU/SoftClub compatible)

Widescreen fix for **Need for Speed: Underground** (PC, v1.4), based on the NFSU part of
[ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) as of commit
[`17c4408`](https://github.com/ThirteenAG/WidescreenFixesPack/tree/17c4408) (July 2023).

## Why this version

Newer versions of the original fix were rewritten (2026) and changed the way they hook into the game.
This version stays with the 2023 code base and focuses on:

- working together with other plugins for NFSU, e.g. the NFSU Lap Recorder
- the European (EU) and the Russian SoftClub release of the game, in addition to North America

## Status

- North America and EU: work, same behavior as the 2023 version.
- Russian SoftClub release: works, including the windowed modes (this release reads the window size from the
  wrong place; the fix corrects that), SkipIntro and the HUD. Not available there yet:
  - `ShowLangSelect`: the game's own language menu is broken in this release, so the option is ignored
  - `HideDebugObjects = 2` (pillarboxing): falls back to 1
  - `BlackMagazineFix`: ignored
  - `ImproveGamepadSupport`: works (button icons, front-end actions), but the controls setup keeps the raw
    DirectInput names ("Button 0" instead of "A")

It is a drop-in replacement: same file names (`scripts\NFSUnderground.WidescreenFix.asi` / `.ini` / `.dat`),
existing settings keep working.

## Installation

1. Install an ASI loader (e.g. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)) as `dinput8.dll` in the game folder.
2. Copy the contents of `data/NFSUnderground.WidescreenFix/` (or the release archive) into the game folder.
3. Adjust `scripts\NFSUnderground.WidescreenFix.ini` if needed.

## Building

Requires Visual Studio (2019 or newer, C++ desktop workload) on Windows with the **MSVC v142** (VS 2019) build
tools. The project is fixed to that toolset because newer ones produce an .asi that doesn't load on Windows 7;
in Visual Studio 2022/2026 add it via the installer (individual component "MSVC v142 - VS 2019 C++ x64/x86
build tools").

```
git clone --recursive https://github.com/Pelorojo/NFSUnderground.WidescreenFix-Legacy.git
cd NFSUnderground.WidescreenFix-Legacy
premake5.bat
```

Then open `build/NFSUnderground.WidescreenFix-Legacy.sln` and build `Release|Win32`. The .asi ends up in
`data/NFSUnderground.WidescreenFix/scripts/`. Optionally set the environment variable `GAME_DIR` (game folder,
with a trailing backslash) before running `premake5.bat` to have the .asi copied into the game's `scripts` folder
after each build.

## Credits and license

- Original work: [ThirteenAG](https://github.com/ThirteenAG) and the contributors of the WidescreenFixesPack.
- Libraries (git submodules): Hooking.Patterns, injector, IniReader, spdlog, filewatch, ModUtils, minidx9 – each under its own license.

MIT License, see [license](license). The original copyright notice is kept unchanged.
