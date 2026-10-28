# Desktop Melee for Windows

Play native Super Smash Bros. Melee over your Windows desktop. The taskbar is
the main stage, open window tops become moving platforms, and optional
wallpaper analysis turns clear terrain outlines into connected slopes.
The desktop stays clickable underneath the fighters.

**Experimental Windows x64 preview · version 0.1.6.**

## Download and play

**[Download the Windows installer](https://github.com/Bluesguitartravis-droid/desktop-melee/releases/latest/download/Desktop-Melee-Setup-x64.exe)**

[All releases and checksums](https://github.com/Bluesguitartravis-droid/desktop-melee/releases)
· [Full setup, controls and troubleshooting](docs/DESKTOP_MELEE.md)

1. Download and run **Desktop-Melee-Setup-x64.exe**.
2. Open **Desktop Melee** from its shortcut.
3. Select your own local **Melee USA 1.02** disc image.
4. Choose **Play against a CPU**, **Watch two CPUs**, or **Two local players**.
5. Start a desktop match. Use a controller while other apps remain clickable.

The single installer includes the engine, DLLs and resources and installs them
together for your Windows account. No administrator access is required. An
optional portable ZIP is also available; extract that entire archive and open
its **DesktopMelee.exe**. The source ZIP is for developers. **No disc image or extracted game assets are supplied.** Your
disc remains on your own computer and is not uploaded.

Use a primary display with a visible bottom taskbar for the first run. This
package targets 64-bit Windows 10 (1903 or newer) and Windows 11 on Intel/AMD
PCs. Other desktop-overlay platforms are not implemented in this fork.

## Startup updates

**DesktopMelee.exe checks this repository's public releases every time it
opens.** The check runs in the background and does not hold up launching a
match. A newer compatible release produces a **Download update** button that
opens the Windows installer download in your browser. You can also check again
manually.

Close the match and launcher, then run the new installer. It updates the
program files while retaining settings and saves. Launcher choices are kept
in `%LOCALAPPDATA%\DesktopMelee\settings.ini`. The default installation is
`%LOCALAPPDATA%\Programs\DesktopMelee`. Your disc image stays where you put it.

Update checking needs no GitHub account. Offline and rate-limit errors leave
the game usable. The app checks stable `desktop-vMAJOR.MINOR.PATCH` releases,
ignores GitHub prereleases, and never installs or replaces files automatically.
The check sends no game image, wallpaper, window contents, saves or logs.

## Desktop geometry

- **Taskbar floor:** Final Destination's native floor spans the taskbar, with
  screen edges as the knockout boundaries.
- **Window platforms:** exposed top edges of normal app windows are one-way
  floors. Moving and resizing a window moves its platform; closing or
  minimizing it makes fighters fall. Separate browser windows are separate
  platforms; tabs inside one window share the same top edge.
- **Wallpaper terrain:** optional image analysis traces a clear ground outline
  into connected native floor segments. Broad hills and contrasting pixel art
  work better than busy photographs. Covered terrain is hidden by app windows.
- **Native match behavior:** CPU opponents, stocks, rematches, character and
  costume choices, controller ports and UCF settings.

Wallpaper recognition is a bounded heuristic. It does not create solid walls,
ceilings or hazards, and it cannot guarantee every ledge is reachable. The
0.1.6 build includes continuous hill contours and a correction that lets native
floor collision resolve before a desktop bottom knockout is considered.

## Controls

| Action | Control |
|---|---|
| Pause and open settings | Ctrl+Shift+F10 |
| Resume from settings | F1 |
| Quit the desktop match | Ctrl+Shift+F11 |
| Open tray controls | Right-click the Desktop Melee tray icon |

## Build and publish

No disc is needed to compile the project. On Ubuntu 24.04 with the POSIX
MinGW-w64 x64 toolchain, CMake, Ninja and the dependencies documented in the
[desktop guide](docs/DESKTOP_MELEE.md#build-from-source):

```bash
bash tools/build_desktop_windows.sh
```

The main output is `dist/Desktop-Melee-Setup-x64.exe`, with an optional
`dist/Desktop-Melee-Windows-x64.zip`. Building the installer also needs NSIS
(`makensis`).

To publish, commit a new `DESKTOP_VERSION`, then run **Actions → Desktop
Windows release** with its matching tag, such as `desktop-v0.1.7`. Pushing that
tag also works. The workflow runs regression checks, builds Windows x64, and
publishes the Windows installer, portable ZIP, matching source ZIP and SHA-256
checksums.
The built-in workflow token is used by GitHub Actions; no token is included in
the application. The inherited upstream build and Pages workflows are manual.

## Status and reporting problems

The window-platform preview has been run successfully by the initial tester.
The newer terrain, throw-ordering and updater changes have automated checks
and a Windows cross-build, but their live behavior still needs confirmation
on Windows. This is an experimental project; controller compatibility,
presentation performance and arbitrary desktop layouts need wider testing.

Use this repository's issue tracker for Desktop Melee issues. Include the
relevant section of `desktop-melee.log`, Windows/GPU details and display
scaling. Do not attach a game disc image.

## Credits and licensing

This is an independent Windows implementation of the desktop-fighting idea,
built on [999sian/melee-pc](https://github.com/999sian/melee-pc), pinned initially
at `0fb0af3304d3469308b5574369a6ec86f145632f`. The base port uses
[doldecomp/melee](https://github.com/doldecomp/melee),
[Aurora](https://github.com/encounter/aurora), Dawn and SDL3.

Existing notices and history are retained. The port code is GPL-3.0-or-later;
the decompiled game code and third-party components have different licensing
status. See [the full licensing explanation](licenses/LICENSE.md),
[COPYING](licenses/COPYING) and the notices included in the Windows package. This
project is not affiliated with Nintendo or HAL Laboratory.
