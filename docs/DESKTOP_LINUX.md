# Desktop Melee on Linux and Steam Deck

**Linux preview 1 · version 0.1.7 · x86_64.** This is a native Linux build;
the Windows installer and Proton are not needed. The desktop overlay in this
preview requires a **composited X11 desktop session**. Steam Deck hardware
and real matches still need testing by someone with the device and a disc.

## Choose the right mode

| Environment | This preview |
|---|---|
| KDE Plasma on X11, including Steam Deck X11 Desktop Mode | Transparent desktop match, panel floor and moving window platforms |
| Other X11 desktops with compositing and standard window-manager properties | Same implementation; desktop-specific testing is still needed |
| KDE Plasma on Wayland or another Wayland desktop | Ordinary game window; desktop overlay is unavailable |
| Steam Deck Gaming Mode / Gamescope | Ordinary game window; desktop overlay is unavailable |

The launcher identifies unsupported sessions and explains the limitation.
Choosing an ordinary game window is explicit. Forcing `SDL_VIDEODRIVER=x11`
inside a Wayland session does not make it an X11 desktop: native Wayland
windows remain outside the X11 window list.

SteamOS 3.8 changed Desktop Mode to Wayland by default. Valve documents a
selectable X11 desktop session through Steam's Developer settings. See the
[SteamOS 3.8 announcement](https://steamcommunity.com/games/1675200/announcements/detail/697641379212298073)
and [Steam Deck Desktop FAQ](https://help.steampowered.com/en/faqs/view/671A-4453-E8D2-323C).
The X11 selection is a desktop-session setting; this app does not alter it.

## Steam Deck setup

1. In Steam's settings, enable Developer settings if needed and select the
   **X11 / Legacy desktop session**. The exact label depends on the SteamOS
   release. This is required for desktop matches in this preview.
2. Choose **STEAM → Power → Switch to Desktop**. If already on the desktop,
   save your work, return to Gaming Mode and enter Desktop Mode again after
   changing the session setting.
3. Download **Desktop-Melee-Linux-x86_64.AppImage** from the
   [Linux preview release](https://github.com/Bluesguitartravis-droid/desktop-melee/releases/tag/desktop-v0.1.7-linux.1).
4. In Dolphin, open the file's **Properties → Permissions** and allow it to
   run as a program. Open the AppImage.
5. Select your own local **Melee USA 1.02** disc image, choose the players or
   CPU watch mode, and start the desktop match.

The disc can stay on your Deck's internal storage or another readable drive.
It is not uploaded, bundled into the application or needed to build the code.
The app does not require administrator access, package-manager installation
or disabling SteamOS's read-only system protection.

Keep Steam running for the Deck's controls. Valve's desktop FAQ confirms
that applications can use the built-in controls while Steam runs. An
external controller is also an option. For play through Steam, add the
AppImage as a **Non-Steam Game** and use a gamepad controller layout. Avoid
forcing a Proton compatibility tool for this native Linux build. Actual
controller enumeration, mappings and background input on Deck remain
hardware-test items.

If you only want an ordinary Melee window in Gaming Mode, the same Non-Steam
Game entry can launch it. Open the launcher's regular-window option. The
Steam Library interface is not a KDE desktop with ordinary app windows, so
the panel and window-platform mode is unavailable there.

## Download formats

**The AppImage is the single-file download.** It contains the launcher, game
engine, runtime libraries, fonts and other non-game resources. Your Linux
graphics driver and basic system libraries still come from the operating
system. This preview targets 64-bit Intel/AMD systems; it is not an ARM build.

The Ubuntu 24.04-based release requires **glibc 2.39 or newer** and a
`libstdc++.so.6` providing **GLIBCXX_3.4.32**. These
requirements were checked on the packaged ELF binaries. Use a current
SteamOS installation or a comparable modern Linux distribution. An
AppImage does not make this build compatible with older system libraries.

If your distribution cannot mount an AppImage, use
**Desktop-Melee-Linux-x86_64.tar.gz** instead. Extract the entire archive, open
the resulting folder and run **AppRun**. Keep `usr`, the resources and the
other packaged files together. The source ZIP is for development.

The AppImage runtime also supports extraction without FUSE:

```bash
chmod +x Desktop-Melee-Linux-x86_64.AppImage
./Desktop-Melee-Linux-x86_64.AppImage --appimage-extract-and-run
```

`Desktop-Melee-Linux-SHA256SUMS` accompanies each Linux preview. To verify
the downloaded assets from a terminal in their folder:

```bash
sha256sum --check --ignore-missing Desktop-Melee-Linux-SHA256SUMS
```

## Desktop geometry and controls

The primary monitor becomes the arena. The visible bottom panel is used as
the floor; a conservative floor near the screen bottom is used when no
usable bottom panel is available. Screen edges are the knockout boundaries.
Exposed top edges of ordinary windows become one-way platforms. Moving,
resizing, minimizing and closing a window update its platform. Overlapping
windows clip covered parts of other windows' top edges.

Separate browser windows can become separate platforms. Tabs within one
browser window share its one top edge. No titles, browser content, document
text or screenshots are needed to derive these rectangles.

Linux wallpaper recognition is **not included in this preview**. Its Windows
implementation needs a Linux-specific source/layout adapter before the
same contour analysis can be enabled reliably. Wallpaper imagery still
remains visible behind the fighters.

| Action | Control |
|---|---|
| Pause and open settings | Ctrl+Shift+F10 |
| Resume from the in-game settings window | F1 |
| Quit the desktop match | Ctrl+Shift+F11 |

Use a keyboard or map the shortcuts to spare controller buttons through
Steam Input. The launcher also provides match controls while a child game
process is running. The desktop overlay never takes mouse focus; normal
clicks continue to reach your desktop apps.

Use a **visible, fixed bottom panel** during a desktop match. Turn off panel
auto-hide or adaptive/floating behavior for this preview. The game exits
the match if the monitor or panel geometry changes, because its existing
stage collision must stay aligned with the screen. Moving ordinary app
windows remains supported.

Keep the monitor arrangement, scaling and panel position stable during a
match. Close the match and relaunch after changing the display layout.
Start with the Deck's built-in display or one primary monitor before trying
more complex docked layouts.

## Saves, preferences and updates

Linux launcher preferences are stored under
`$XDG_CONFIG_HOME/DesktopMelee/settings.conf`, normally
`~/.config/DesktopMelee/settings.conf`. Game saves, caches and the game log
use `$XDG_DATA_HOME/DesktopMelee`, normally `~/.local/share/DesktopMelee`,
separate from the downloadable AppImage. Replacing the AppImage does not
replace the disc or these files.

On startup, the launcher checks this repository's public releases in the
background. It accepts compatible Linux preview tags such as
`desktop-v0.1.7-linux.1`, and later stable releases with a Linux artifact.
Windows-only releases are ignored. A newer compatible version offers the
AppImage download; a portable archive is available as a fallback.

Close the match and launcher before replacing the AppImage with the newer
file. This preview announces downloads; it does not install updates
automatically. Offline, failed and rate-limited requests leave the launcher
usable. No GitHub account is needed. The request does not include your disc,
wallpaper, window contents, saved games or logs.

Linux previews are published as GitHub prereleases. They do not replace the
Windows stable release or trigger the Windows launcher's update prompt.

## Troubleshooting

**The launcher says Wayland or Gaming Mode is unsupported.** Use the ordinary
game-window option, or change to a real X11 Desktop Mode session before
starting a desktop match. An X11 compatibility window inside Wayland is
insufficient. In a desktop terminal, `printf '%s\n' "$XDG_SESSION_TYPE"`
normally reports `x11` or `wayland`.

**The AppImage does not open.** Check its executable permission. If the
system reports a FUSE/mount error, use extraction mode or the portable
tarball. A message about an unavailable `GLIBC` version means the host's
system libraries are older than this build supports; extracting the
AppImage does not change that requirement.

**The desktop match does not become transparent.** Ensure desktop
compositing is enabled. The overlay needs an alpha-capable composited X11
desktop and X Shape support for click-through input. The game reports a
failure instead of placing an opaque input-blocking layer over your apps.

**A window has no platform.** The window must be visible on the current
desktop, expose a usable top edge, and be an ordinary app window. Panels,
desktop surfaces, the game's own windows and minimized windows are not
moving platforms. A maximized window with a top edge at the knockout
boundary may offer no reachable floor.

**The Deck's buttons act like desktop keys.** Check the active Steam Input
layout for the app and switch to a gamepad layout. The launcher may be
operated with the trackpad, or with a keyboard and mouse while establishing
the gamepad configuration. Do not disable system protections or install
unrelated controller drivers for this preview.

Report problems in this repository's issue tracker with the Linux version,
desktop/session type, GPU, monitor layout, launcher version and relevant log
lines. Do not attach a disc image. Steam Deck performance, battery life and
suspend/resume behavior have not been measured by the automated build.

## Build from source

No disc is required to compile the project. The release workflow uses
Ubuntu 24.04, GCC, CMake, Ninja, Python 3, a vendored SDL3 build, and prebuilt
Dawn and nod dependencies. Development packages include SDL's X11/Wayland,
audio and input dependencies, plus libcurl, OpenSSL and XCB render/shape/randr.
The complete dependency list is in
[the Linux workflow](https://github.com/Bluesguitartravis-droid/desktop-melee/blob/master/.github/workflows/desktop-linux.yml).

```bash
bash tools/build_desktop_linux.sh
```

The default build directory is `build-linux`; outputs are under `dist/linux`.
`BUILD_DIR`, `OUTPUT_DIR` and `BUILD_JOBS` can override those defaults. The
build script creates both the AppImage and the portable archive and binds
their contents to a build manifest. It does not fetch game data.

Packaging tools are pinned by SHA-256 in `tools/linux/packaging-tools.json`.
Their upstream `continuous` download URLs can change; a changed tool fails
verification. A developer must inspect a new upstream version and update
the pin deliberately before building with it. The script does not accept a
new hash automatically.

To publish a preview, commit the source, set `DESKTOP_VERSION` and
`DESKTOP_LINUX_PREVIEW`, then run **Actions → Desktop Linux preview** with
the exact matching tag, for example `desktop-v0.1.7-linux.1`. The workflow
checks native collision logic, release identity, X11 presentation and
disc-free startup before publishing. It includes a source ZIP generated
from the exact tested commit, with SHA-256 checksums. Existing release
assets are not overwritten.

The X11 tests run in a virtual display with compositing. A separate renderer
smoke check uses Mesa's software Vulkan driver to enable the overlay,
present actual GPU readbacks, return to the ordinary window, and enable
the overlay again before shutdown. These exercise the actual Linux backend
without game data, but do not substitute for a match on a Steam Deck. The
Windows stable workflow remains separate.
