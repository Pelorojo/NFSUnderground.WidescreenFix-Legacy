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
  wrong place; the fix corrects that), SkipIntro and the HUD. Only `ShowLangSelect` is ignored there: the game's
  own language menu is broken in this release.

It is a drop-in replacement: same file names (`scripts\NFSUnderground.WidescreenFix.asi` / `.ini` / `.dat`),
existing settings keep working.

## Changes

### v1.4

- New `MSAA` option (`[GRAPHICS]`, off by default): multisample antialiasing, the highest level up to the set one
  (2, 4, 8) the graphics card can do. With motion blur or light trails the game draws the scene into a texture,
  which can't be multisampled and breaks the picture (buildings and cars could be seen through), so with MSAA the
  game treats both as unsupported: they're off and greyed out in its options. F12 screenshots of the LapRecorder
  work with MSAA from its next version on. With DXVK or dgVoodoo the game's MSAA has to be left to the game, other
  values there override it: DXVK `d3d9.forceSwapchainMSAA = -1` (or not set) in `dxvk.conf`, dgVoodoo
  `Antialiasing = appdriven` in the `[DirectX]` section of `dgVoodoo.conf`.
- `BlackMagazineFix` works in all three releases now (the Russian SoftClub one too) and is on by default: motion
  blur and light trails are switched off while a magazine cover is shown and back on when it closes, the game
  rebuilds its render targets for that by itself. The former fix (moving the screen clears) is gone.
- `ImproveGamepadSupport`: the right stick turns the car in the menus also with a second controller plugged in
  (e.g. a joystick): the game passed on the other device's axes and every device's call overwrote the turn. The
  stick is read from the XInput gamepad directly (`XInput9_1_0.dll`, part of Windows) and the turn worked out once a
  frame; without an XInput gamepad the game's axes are used as before.
- New `Language` option (`[MISC]`): the game's language without the registry entry it normally reads
  (`Language` under `HKLM\Software\EA Games\Need For Speed Underground` or `...\NFS Underground`), a name from its
  language table, e.g. `English US`, `German`, `Russian` (the Russian SoftClub release only). `0` keeps the
  registry's. Without a registry entry the Russian SoftClub release now starts in Russian instead of English.
- `HideDebugObjects = 2` (pillarboxing) is gone: it never ran together with the LapRecorder (both hook the same
  spot) and `ClipFrontend` hides the same. `2` counts as `1` now, which is also the default when the line is missing.
  `HideDebugObjects` has no effect with `ClipFrontend = 1`.

### v1.3

- `ImproveGamepadSupport`: the right stick turns the car in the menus, like dragging with the mouse (without the
  mouse click sound).
- "vinyl color" on the vinyl layer screen was mouse only: C (keyboard) or the pad's Select opens it now, the same
  button as "Customize Car". With the LapRecorder its icon is shown there.

### v1.2

- New `SaveKeyboardBindings` option (`[MISC]`, on by default): the game keeps its keyboard bindings through
  DirectInput's action mapping, which saves them via `dimap.dll`. Windows doesn't ship it anymore (and with it the
  game freezes on some PCs), so the keyboard was back to the defaults on every start. The bindings are now saved in
  `Keyboard.map` next to the profiles and put back on start. `SaveKeyboardBindings = 0` turns it off.
- The game's own resolution setting (Options > Display) is fixed to 800x600, its default: with the fix the
  resolution comes from `ResX`/`ResY` (or the desktop), the setting did nothing but could be changed anyway. The
  arrows are gone and the menu shows the resolution the game actually renders at.
- `ClipFrontend` also clips the race results to 16:10 with `HUDWidescreenMode = 0`, so texts the game parks just
  outside of them (drift scores, placeholder times) no longer show. With `HUDWidescreenMode = 0` the objects parked
  outside the 4:3 HUD no longer show in the pillarbox area either.
- `FixHUD = 0` switches `HUDWidescreenMode` and `ClipFrontend` off, nothing sits in a 4:3 area then.
- `ImproveGamepadSupport`:
  - button names are matched by their exact number: buttons from 10 on showed as "B", the Xbox controllers'
    Share button ("Button 11") is now "Share"
  - the controls setup shows the gamepad names in the Russian SoftClub release too, also with localized
    (e.g. Russian) DirectInput names
  - the keyboard page keeps the key names (e.g. "Num 4" became "LB")
- An empty or non-number value in the ini (e.g. `FixHUD =`) counts as missing, the default is used. The game
  crashed at start before.

### v1.1

- New `ClipFrontend` option (`[MISC]`, on by default): on screens wider than 4:3 the menus are clipped to the 4:3
  area, so what the game draws outside of it no longer shows, e.g. the leftover tags on the online login or the
  neighbor cars' names in the car select. The mouse cursor still shows on the whole screen. `ClipFrontend = 0`
  turns it off.
- `FPSLimit` is off by default (`0`), `LeftStickDeadzone` is `0.0` by default, also when the line is missing in
  the ini.

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
