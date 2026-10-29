# Desktop Melee

Play native Super Smash Bros. Melee over your desktop. The taskbar or panel
is the main stage and open window tops become moving platforms. The desktop
stays clickable underneath the fighters. The Windows build also offers
experimental wallpaper terrain.

**Windows x64: 0.1.10 · Linux x86_64: 0.1.7 Linux preview 1.**

## Download and play on Windows

**[Download the Windows installer](https://github.com/Bluesguitartravis-droid/desktop-melee/releases/latest/download/Desktop-Melee-Setup-x64.exe)**

[All releases and checksums](https://github.com/Bluesguitartravis-droid/desktop-melee/releases)
· [Full setup, controls and troubleshooting](docs/DESKTOP_MELEE.md)
· [Staggered all-monitor span mode](docs/DESKTOP_WINDOWS_010.md)

1. Download and run **Desktop-Melee-Setup-x64.exe**.
2. Open **Desktop Melee** from its shortcut.
3. Select your own local **Melee USA 1.02** disc image.
4. Choose one **Display** or enable **Span match across all monitors**, then choose **2, 3 or 4 fighters**.
5. Choose **Watch CPUs**, **Player 1 vs. CPUs**, **Local players**, or a custom mix.
6. Start a desktop match. Use a controller while other apps remain clickable.

Each fighter has a character, costume, and CPU/controller-port selection.
Human fighters need different controller ports. Matches are free-for-all;
team battles are not added in this release. Single-display mode keeps wallpaper
terrain. All-monitor mode spans the Windows virtual desktop and keeps window
platforms across the span; wallpaper terrain is disabled there for now. Stop
the match before changing display selection or span mode.

The single installer includes the engine, DLLs and resources and installs them
together for your Windows account. No administrator access is required. An
optional portable ZIP is also available; extract that entire archive and open
its **DesktopMelee.exe**. The source ZIP is for developers. **No disc image or
extracted game assets are supplied.** Your disc remains on your own computer
and is not uploaded.

Use a visible, fixed bottom taskbar for the first run. A single screen without
a usable bottom taskbar gets a floor near the bottom. In all-monitor mode the lowest physical display supplies the main floor and
every higher display contributes its own broad one-way floor/platform. This
supports staggered monitor stacks instead of requiring one shared horizontal
band across every display.
This package targets 64-bit Windows 10 (1903 or newer) and Windows 11 on Intel/AMD
PCs. Live multi-monitor and controller behavior still need testing on Windows.

## Linux and Steam Deck

**[Download the Linux AppImage](https://github.com/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7-linux.1/Desktop-Melee-Linux-x86_64.AppImage)**

[Linux setup, Steam Deck instructions and limitations](docs/DESKTOP_LINUX.md)
· [Linux preview assets and checksums](https://github.com/Bluesguitartravis-droid/desktop-melee/releases/tag/desktop-v0.1.7-linux.1)

The Linux preview is a native, single-file AppImage. Mark it executable in
the file properties and open it. A complete portable tarball is also
available. Choose a local USA 1.02 disc image in the launcher; no game data
is distributed or uploaded.

**The desktop overlay requires a composited X11 desktop session.** Current
SteamOS uses Wayland by default. For the desktop effect on Steam Deck,
select the X11/Legacy desktop session in Steam's Developer settings, then
enter Desktop Mode. The launcher offers an ordinary game window under
Wayland and Gaming Mode. Drawing fighters over those interfaces is not
implemented in this preview. Linux wallpaper terrain is also pending.

Linux native builds, collision regressions and virtual-X11 integration
checks are automated. This preview has not been certified on Steam Deck
hardware or tested through a real match there. The published Linux and
Android builds are unchanged by the Windows 0.1.8 update; their new feature
ports are deferred until the PC version has been tested.

## Startup updates

The Linux launcher checks for compatible Linux releases, including its
preview channel, and offers the AppImage download. It ignores Windows-only
releases. Linux preview releases remain separate from Windows stable
updates. See the [Linux update instructions](docs/DESKTOP_LINUX.md#saves-preferences-and-updates).

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
- **Wallpaper terrain (Windows):** optional image analysis traces a clear ground outline
  into connected native floor segments. Broad hills and contrasting pixel art
  work better than busy photographs. Covered terrain is hidden by app windows.
- **Native match behavior:** CPU opponents, stocks, rematches, character and
  costume choices, controller ports and UCF settings. Windows supports 2-4
  fighters, selecting one arena display, or spanning all connected monitors.

Wallpaper recognition is a bounded heuristic. It does not create solid walls,
ceilings or hazards, and it cannot guarantee every ledge is reachable. The
0.1.6 changes for continuous hill contours and native floor collision before
bottom knockout checks remain included in 0.1.10. Multi-monitor wallpaper
Tile/Span placement remains unsupported.

## Controls

| Action | Control |
|---|---|
| Pause and open settings | Ctrl+Shift+F10 |
| Resume from settings | F1 |
| Quit the desktop match | Ctrl+Shift+F11 |
| Open tray controls (Windows) | Right-click the Desktop Melee tray icon |

The Linux launcher also has a **Stop match** button. Global overlay
shortcuts require an X11 session. Map them to spare controller buttons if
using a Steam Deck without a keyboard.

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
Windows release** with its matching, unpublished tag. Pushing that tag also
works. The workflow runs regression checks, builds Windows x64, and publishes
the installer, portable ZIP, matching source ZIP and SHA-256 checksums.
Existing release assets are not overwritten. The built-in workflow token is
used by GitHub Actions; no token is included in the application. The inherited
upstream build and Pages workflows are manual.

For Linux, run `bash tools/build_desktop_linux.sh`. Its outputs are an
AppImage and portable tarball under `dist/linux`. The separate **Desktop
Linux preview** workflow validates `DESKTOP_VERSION` and
`DESKTOP_LINUX_PREVIEW`, tests the native backend, and publishes a GitHub
prerelease with matching source and checksums. See the
[Linux build guide](docs/DESKTOP_LINUX.md#build-from-source).

## Status and reporting problems

The window-platform preview has been run successfully by the initial tester.
Four-fighter match setup, spawn/respawn positions, CPU targeting and display
geometry have automated regressions, alongside the earlier terrain and
throw-ordering fixes. The Windows binaries are cross-compiled; live behavior,
controller enumeration, performance and arbitrary desktop layouts still need
wider Windows testing. This is an experimental project.

Use this repository's issue tracker for Desktop Melee issues. Include the
relevant section of `desktop-melee.log`, operating system/GPU details and
display scaling. On Linux, include the desktop and X11/Wayland session type.
Do not attach a game disc image.

## Credits and licensing

This is an independent implementation of the desktop-fighting idea,
built on [999sian/melee-pc](https://github.com/999sian/melee-pc), pinned initially
at `0fb0af3304d3469308b5574369a6ec86f145632f`. The base port uses
[doldecomp/melee](https://github.com/doldecomp/melee),
[Aurora](https://github.com/encounter/aurora), Dawn and SDL3.

Existing notices and history are retained. The port code is GPL-3.0-or-later;
the decompiled game code and third-party components have different licensing
status. See [the full licensing explanation](licenses/LICENSE.md),
[COPYING](licenses/COPYING) and the notices included in each package. This
project is not affiliated with Nintendo or HAL Laboratory.
