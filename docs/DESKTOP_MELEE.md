# Desktop Melee for Windows — 0.1.6 / experimental preview 6

An independent Windows implementation of the desktop-fighting idea: native
Super Smash Bros. Melee, a transparent overlay, a stage aligned with the
primary monitor's taskbar, open window tops as moving platforms, and optional
platforms generated from the wallpaper image.
Built on `999sian/melee-pc` at commit
`0fb0af3304d3469308b5574369a6ec86f145632f`.

**Status:** this package is a development preview. The user has confirmed that
preview 4 runs and that open-window platforms work well. Version 0.1.6 adds
automatic startup update checks and includes the preview 5 wallpaper-terrain
and taskbar collision-ordering corrections.
The Windows executables are cross-compiled. Automated checks exercise image
geometry, native collision, CPU targeting and the knockout/collision order.
This build has not been played on a Windows PC in the build environment;
controller feel, real-match throws and live wallpaper changes still require
testing on the user's computer with their own local disc image.

## Preview 6: GitHub releases and startup update checks

Project: <https://github.com/Bluesguitartravis-droid/desktop-melee>

Download and run **Desktop-Melee-Setup-x64.exe** from the project's
[Releases page](https://github.com/Bluesguitartravis-droid/desktop-melee/releases).
This one installer includes the engine, DLLs and resources, installs them for
your Windows account and creates a shortcut. It does not require administrator
access. **Desktop-Melee-Windows-x64.zip** is an optional portable alternative.
The separate source ZIP is for building the app, not for launching it.

Every time **DesktopMelee.exe** opens, it checks this repository's public
GitHub releases in the background. The launcher shows its current version and
one of these results:

- **Up to date:** no newer compatible stable desktop release was found.
- **Download update:** opens the newer release's Windows installer download
  in your browser (or its portable ZIP if no installer is attached).
- **View release:** opens the project's release page if its Windows asset is
  not available yet.
- **Could not check:** the app remains playable; use **Check for updates** to
  retry later. Offline, timeouts and rate limits do not block starting a match.

The checker uses numeric versions under `desktop-vMAJOR.MINOR.PATCH`, skips
prereleases and unrelated upstream releases, and never asks for a GitHub login.
It checks once per launcher startup, plus explicit manual retries. It does not
run a recurring updater while the launcher is closed.

To install an update, stop the match, close the old launcher, and run the new
**Desktop-Melee-Setup-x64.exe**. It updates program files in the existing install
folder while preserving the `user` folder and launcher settings. The default
installation is `%LOCALAPPDATA%\Programs\DesktopMelee`. Launch **Desktop Melee**
from its shortcut after setup finishes. Your selected disc and launcher choices
remain in `%LOCALAPPDATA%\DesktopMelee\settings.ini`. Keep your disc image in
its existing location; no upload is needed.

If you prefer the portable ZIP, extract the complete new ZIP into its own
folder and open **DesktopMelee.exe**. Keep the old folder until the new build
works. To retain portable game settings and saves when moving from an older
ZIP to another folder or to the installed app, copy the old `user` folder into
the new game folder while both games are closed. The launcher's **Open game
folder** button opens that location.

This version checks and offers the download; installing the new package is a
manual step. It never overwrites a running executable. Update checks contact
GitHub over HTTPS for public release metadata. They do not send the disc path,
disc contents, wallpaper, window titles, saves or logs. The upstream port's
separate updater remains disabled so it cannot replace the desktop fork.

## Preview 5 fixes included: wallpaper hills and taskbar landings

The wallpaper detector can now follow a continuous terrain outline and turn
it into connected slopes. A clear hill against the sky can provide a walkable
surface along its curve instead of only at a flat hilltop. The segments use
Melee's native floor collision, including its floor normals and connections.
Window tops keep their existing moving-platform behavior.

The bottom-knockout check now waits for desktop floor collision to resolve.
Previously, a fast downward movement could cross both the taskbar and the
screen's lower boundary in one physics step, triggering a knockout before
the floor got a chance to stop it. This correction preserves the floor and
screen-edge boundaries. A fighter that remains below the bottom boundary
after collision is still knocked out.

Regular down-throw animations also receive a narrowly scoped guard while the
victim is still attached to a grounded thrower over an enabled supporting
floor. Native throw-release collision restores the released fighter to the
floor, and normal knockout checks resume immediately. Individual character
animations still need confirmation in a real match.

## Preview 4: open windows become platforms

The launcher now has **Use open window tops as platforms**, enabled by default.
Visible app windows on the primary display contribute their top edges as
one-way platforms. Fighters can jump through from below, land on top, and use
Melee's usual drop-through controls. The taskbar remains the main floor.

Moving a window moves its platform. Resizing updates the available landing
area. Closing or minimizing a window removes its platform, so a fighter that
was standing there falls. Overlapping windows leave only the exposed portions
of lower window tops available. CPU floor queries use the same collision
surfaces as human players.

For a browser, the **outer browser window** provides the platform. Tabs within
that window share it. Drag a tab into its own window to give it a separate
platform. Page content, buttons, icons and widgets do not become geometry.

### Try the new platforms

1. Run the Windows installer and open its **Desktop Melee** shortcut (or
   extract the portable ZIP and open **DesktopMelee.exe**). The launcher should say **WINDOWS PREVIEW 6 / VERSION 0.1.6**.
2. Leave **Use open window tops as platforms** checked and select your local
   USA 1.02 disc image. It can stay in its existing location.
3. Restore a browser or File Explorer window from maximized mode, make it
   fairly small, and position its top a short jump above the taskbar. Add a
   second window slightly higher to make steps.
4. Start a match, jump onto a window top, then move, resize or minimize that
   window to exercise the moving geometry. **Play against a CPU** with a
   controller gives you direct control of this first test.

The windows must be within the fighter's reach. A maximized window's top is at
the upper screen boundary, so it is not a useful first platform. Standard CPU
behavior can use the geometry but is not a custom solver for every possible
arrangement of windows.

Stop the match and uncheck **Use open window tops as platforms** to return to
the taskbar-only arena when wallpaper terrain is also off. The launcher
remembers both choices independently.

## Generate a level from your wallpaper

Enable **Generate platforms from wallpaper (experimental)** in the launcher to
derive up to **32 additional one-way floor segments** from the current Windows wallpaper.
This option starts off, since many wallpapers do not contain suitable terrain.
It can be used together with window platforms or on its own.

The detector works best with a clear foreground separated from the background:
simple pixel art, contrasting silhouettes, colored blocks, or a broad hill
against the sky. It can trace a continuous outline and approximate its curve
with connected straight segments. It also retains detection of broad,
independent horizontal ledges. Fine decorative details are intentionally
omitted. This is a bounded image heuristic, not a general scene recognizer;
busy photos, ambiguous colors and perspective can still produce missing or
unwanted surfaces. The ground outline in the supplied Windows XP hill image
is used as a development regression case; the image is not included here.

On that exact image, the previous detector found one horizontal span covering
321 pixels of a 1920-pixel screen. The new detector finds eight connected
sloping segments spanning the full width, without unrelated flat ledges in
the clouds or grass. That is an image-analysis result, not a claim that the
new build has been played on Windows. The outline remains approximate,
especially where distant terrain overlaps the foreground.

Set a suitable static image as your Windows background, enable the checkbox,
then start the match. Changing the wallpaper during a match triggers another
analysis automatically. Allow a few seconds for the next background check and
decode. A separate worker loads and analyzes the image; gameplay does not wait
for image decoding. The app reads the local wallpaper file and its placement
settings. It does not capture the screen or upload the image.

The selected file must already be available on a local drive; network files
and cloud placeholders that would need downloading are skipped. This preview
bounds input images to 64 megapixels and 256 MiB per file. The reduced analysis
image is at most 640 by 360 pixels.

The image is mapped to the primary display using Windows' **Fill**, **Fit**,
**Stretch** or **Center** placement. **Tile** is supported on a single monitor;
single-monitor **Span** is treated as Fill. Multi-monitor Tile/Span and live
video/third-party animated wallpapers are outside this first version's scope.
Only the exposed parts of the generated terrain remain active when normal app
windows cover the wallpaper. Moving those windows updates visibility without
re-analyzing the background image.

Generated ledges use ordinary Melee floor collision: jump up through them,
land on top, and drop through them. A wallpaper change removes the previous
terrain and installs the newly detected surfaces; it does not carry fighters
from an old level to a different one. The taskbar floor and screen-edge KO
boundaries remain in place. Curves are approximated by connected sloping
segments. Solid walls, ceilings, hazards and automatic reachability of every
surface are not generated in this version.

Reachability still depends on the image and Melee's normal jumps. A hill high
above the taskbar may require a window platform as an intermediate step.

## Preview 3: CPU opponents on a taskbar arena

Melee's CPU uses a vertical safety margin when deciding whether opponents and
floor locations are safely inside the arena. On the reported 1920-by-1080
display with a 40-pixel taskbar, the floor sits only 8.89 game units above the
bottom KO boundary, less than the usual 10-unit margin. That caused grounded
opponents and floor locations to be rejected by the AI.

Desktop matches now adapt that CPU margin to the available space below the
floor. The stage continues to use screen-edge KO boundaries. CPU initialization
applies the adjustment to new fighters, including respawns. Ordinary stages
use their existing CPU settings.

The CPU correction is included in version 0.1.6. **Watch two CPUs** still uses the
ordinary VS CPU behavior and the two fighters chosen in the launcher.

## Preview 2: startup crash correction

The first preview could crash immediately after enabling the desktop overlay.
The reported crash stack reaches Dawn's Direct3D 12 completion cleanup. This
update removes a second GPU event pump introduced on the UI thread and keeps
completion processing on the existing render worker. It also changes frame
readback callbacks to a mode that permits their buffer read/unmap operations.

Desktop mode retains WebGPU validation and buffer robustness checks. The log
identifies the preview and separately reports the first completed GPU readback
and the first frame displayed on the desktop. Both first-frame milestones were
observed in the subsequent Windows test, with no crash recorded before shutdown.

This is our implementation inspired by the Desktop Melee preview shared by
Reddit user Minute-Tone9933. It does not contain that creator's unreleased code.

## Start a match

1. Run **Desktop-Melee-Setup-x64.exe** to install the app for your account.
   For the portable alternative, extract the **entire ZIP** to a writable
   folder and keep its executables, DLLs and resources together.
2. Open the **Desktop Melee** shortcut, or **DesktopMelee.exe** in the portable
   folder. It runs without administrator privileges.
3. Use **Browse** to select your own **Super Smash Bros. Melee USA 1.02** disc
   image. Supported image containers come from the base port: ISO, GCM, CISO,
   RVZ, GCZ, and WIA. The game checks the image when it starts. No game image,
   original game executable, character assets, or extracted disc files are
   supplied in this package.
4. Choose the two fighters, costumes, stock count and mode:
   **Play against a CPU**, **Watch two CPUs**, or **Two local players**.
5. Choose window platforms and optional wallpaper terrain, connect a controller
   for each human player, and choose **Start desktop match**.
   The base game's launcher may appear while the disc is checked and shaders
   prepare. The desktop overlay starts after the match loads.

For the first test, use the primary monitor and a visible taskbar along the
bottom. The floor extends across the taskbar's reported width. With the normal
full-width Windows taskbar, the stage is wider than ordinary Final Destination.

The code keeps normal Melee fighter physics and combat. It changes the camera,
collision geometry, blast zones, spawn locations, stage visibility, and HUD
placement. The two selected fighters use standard local VS CPU/controller logic.
Completed rounds start another match with the same selection.

## Controls

| Action | How |
| --- | --- |
| Play | Controller port 1 and, in two-player mode, port 2 |
| Keep using your desktop | Click normally; the game overlay passes mouse clicks through |
| Pause / open settings | **Ctrl+Shift+F10**, or right-click the Desktop Melee tray icon |
| Resume from settings | **F1**, **Ctrl+Shift+F10**, or the tray menu |
| Quit the overlay | **Ctrl+Shift+F11** while the overlay is active, or **Quit** in its tray menu |
| Stop from the launcher | Restore the launcher from the taskbar and select **Stop match** |
| Change fighters / match mode | Stop the match, change the launcher's selections, and start again |
| Enable / disable window platforms | Stop the match, change **Use open window tops as platforms**, and start again |
| Enable / disable wallpaper terrain | Stop the match, change **Generate platforms from wallpaper (experimental)**, and start again |
| Generate different wallpaper terrain | Change the Windows background; allow a few seconds for analysis |

Opening settings pauses simulation and returns to a normal game window so its
buttons and controller remapping can receive input. Closing settings restores
the transparent overlay. The tray icon may be inside Windows' hidden-icons menu.
The emergency shortcut must be available for the overlay to start; if another
app owns Ctrl+Shift+F11, the game retains a normal window instead.

Controller remapping, audio controls and other base-port options are in the F1
settings window. Keyboard gameplay only receives input when the ordinary game
window has focus; use controllers for the click-through desktop experience.
UCF can be selected before starting. An unavailable alternate costume is
clamped to a valid costume for that fighter; identical fighters are assigned
different colors when possible.

## Requirements and current limits

- **64-bit Windows 10 (version 1903 or newer) or Windows 11 on an Intel/AMD PC.** This build targets x86-64, not
  Windows on ARM, macOS or Linux.
- A GPU that works with the base port's **Direct3D 12 or Vulkan** renderer.
  Direct3D 11 exists upstream but is not a verified fallback for this preview.
- Your own **USA 1.02** disc image. This package does not provide or download it.
- **One primary monitor** is used. Display and taskbar geometry are sampled
  when a match starts. Keep its resolution, DPI and taskbar layout unchanged
  during the session. App window positions update while the match runs.
- A bottom taskbar is the supported layout. Auto-hide and side/top taskbars
  use a fallback lower-edge floor and are not validated to match a visible bar.
- This preview offers **two local fighters**, including CPU-versus-CPU viewing.
  It is for local desktop matches; this custom arena is not a supported netplay
  or competitive setup.
- Window tops are one-way platforms; their sides and bottoms are not walls
  or ceilings. Up to **32 exposed top-edge spans** are supported. Hidden,
  minimized, cloaked and utility windows are skipped. Tiny exposed spans are
  skipped, and window overlaps are treated as rectangles. Rounded corners and
  transparent holes are not traced.
- Window bounds are sampled about **15 times per second**. Movement can appear
  in small steps during a drag, and Windows' window-animation behavior can
  affect the result. The feature reads window positions and visibility, not
  window titles, browser pages, screenshots or document contents.
- Wallpaper analysis is limited to **32 floor segments**, in addition to the 32 window
  spans. The detector uses a reduced image for predictable processing cost, so
  tiny artwork details are intentionally omitted and inferred edges can be a
  few pixels from the original art. It cannot understand arbitrary scene
  geometry or decide whether an arrangement makes a balanced Melee stage.
- This renderer copies completed native GPU frames into a Windows layered
  window. It uses a three-frame asynchronous readback ring and caps the output
  texture at **1920 × 1080** while keeping the display's aspect ratio. Higher
  resolution monitors receive scaled output. The game simulation is still
  paced by the base engine; actual presentation rate and input-to-display
  latency depend on your hardware and remain unmeasured here.
- Standard game rendering and the launcher remain available in `melee.exe`.
  Upstream's updater is disabled in this fork so a base-port download cannot
  replace the custom desktop executable.

## If something goes wrong

Use the tray's **Quit** action or Ctrl+Shift+F11 to leave the overlay. If the
transparent compositor cannot initialize, the code restores the ordinary game
window and reports the failure. You can also close that window.

**The game never appears:** restore the launcher and use **Open game folder**.
Read `desktop-melee.log` there. If Windows reports a missing DLL, run the installer again or re-extract
the entire portable ZIP; moving only an EXE is not supported. If the disc is rejected,
check that it is the USA 1.02 revision.

**No controller input:** use Ctrl+Shift+F10 to open settings and inspect the
controller port and bindings. Support for individual adapters follows the
upstream port; this build has not been tested with a physical adapter.

**Misaligned floor:** stop the game, restore a visible bottom taskbar on the
primary display, then restart. Monitor rearrangement while active requests an
exit rather than moving collision under the fighters.

**A window is not a platform:** check the launcher's window-platform checkbox,
then use a normal, non-minimized app window on the primary display. Its top
must be visible, below the upper screen edge and above the taskbar. Move it
lower if the fighters cannot jump high enough. A window on another virtual
desktop, a tool palette, a menu or a tiny exposed strip is skipped.

**Wallpaper surfaces are missing or unexpected:** use a static image with a
clear terrain outline or broad contrasting ledges, and try **Fit** or **Fill** on the primary
display. Move covering app windows aside. Check `desktop-melee.log` for the
detected ledge count. A flat image or a busy photo may correctly produce no
usable terrain. Uncheck wallpaper generation to keep playing on the taskbar
and window tops.

**Slow or choppy display:** this first version prioritizes working Windows
transparency. GPU readback and desktop composition add overhead. Use a 1080p
primary display for initial evaluation and include the GPU/display details
when reporting results.

The launcher remembers its selections in
`%LOCALAPPDATA%\DesktopMelee\settings.ini`. The portable package's game settings,
cache and saves stay under its own `user` folder. The startup check requests
public release metadata from GitHub; local game and desktop content is not
uploaded. To report a bug, the useful evidence is the relevant log section, a
screenshot, GPU model, Windows version, display resolution and scaling. Do not
send the disc image merely to report a bug.

## Build from source

The supplied source archive contains the exact edited tree and its upstream
revision record. No game disc is needed to compile it. See `docs/building.md`
for the base project's dependencies and supported compiler versions.

On Ubuntu 24.04, the expected build tools include GCC/G++, the **POSIX-threaded
MinGW-w64 x86-64 compiler**, CMake 3.25 or newer, Ninja, Python, Git, curl,
cabextract, make, zip, NSIS (`makensis`) and the development tools required by the vendored
dependencies. The ordinary upstream toolchain expects the MinGW compiler and
sysroot to be on the system paths. A task-local toolchain can instead be passed
through `TOOLCHAIN_FILE`.

```bash
bash tools/build_desktop_windows.sh
```

The wrapper builds the game and native launcher, stages the DLLs and licenses,
checks the non-system PE imports, and creates
`dist/Desktop-Melee-Setup-x64.exe` plus the optional portable
`dist/Desktop-Melee-Windows-x64.zip`. Network access is needed for the pinned
upstream dependency packages and Microsoft's app-local runtime. The code's
build metadata records the exact upstream base revision; release dependencies
are declared in the repository's CMake files.

### Publish a new desktop release

1. Update `DESKTOP_VERSION` with the next numeric version and commit all changes.
2. Open **Actions → Desktop Windows release → Run workflow** in this repository.
3. Choose the intended branch and enter the matching tag, for example
   `desktop-v0.1.7` when the version file is `0.1.7`.
4. The workflow tests and builds Windows x64, bundles its installer, creates
   matching committed-source and SHA-256 files, uploads all assets to a draft,
   then publishes the complete
   release. A failed build does not publish an update. Existing releases are
   never overwritten; use a new version for changed binaries.

Pushing a matching `desktop-v*` tag also triggers the workflow. The published
installer keeps the name `Desktop-Melee-Setup-x64.exe` so installed launchers
can find it. The portable `Desktop-Melee-Windows-x64.zip` remains available. Stable releases appear in the updater; GitHub
prereleases are ignored. These are still experimental desktop builds even
when the release is in the stable update channel.

### Checks that do not need a disc

From the source root on Linux:

```bash
bash tools/test_desktop_game.sh
bash tools/test_desktop_collision.sh
bash tools/test_desktop_fighter.sh
bash tools/test_desktop_update.sh
python3 tools/test_desktop_release.py
c++ -std=c++20 -O2 -Iextern/aurora/include \
  extern/aurora/tests/desktop_window_geometry_test.cpp -o /tmp/desktop_window_geometry_test
/tmp/desktop_window_geometry_test
c++ -std=c++20 -O2 extern/aurora/tests/desktop_wallpaper_geometry_test.cpp \
  -o /tmp/desktop_wallpaper_geometry_test
/tmp/desktop_wallpaper_geometry_test
c++ -std=c++20 -O2 -Iextern/aurora/include \
  extern/aurora/tests/desktop_wallpaper_mapping_test.cpp -o /tmp/desktop_wallpaper_mapping_test
/tmp/desktop_wallpaper_mapping_test
c++ -std=c++20 -O2 extern/aurora/tests/desktop_pixels_test.cpp -o /tmp/desktop_pixels_test
/tmp/desktop_pixels_test
```

The game-side fixture links the actual match defaults, CPU initialization,
opponent-selection code and arena helper against synthetic collision and
fighter data. It checks disc-endian fields, screen projection, controller
ports, option validation, repeated matches and the thin-taskbar targeting
regression. A separate fixture links the native collision and navigation code
to exercise window platforms, movement, removal, slopes, connected floor
traversal and drop-through across segment joins. It also checks that covering
a segment breaks connectivity and that clipped slopes never move their riders.
Window-geometry checks cover clipping, overlapping windows, fragment ordering,
resizing and bounded output. Wallpaper analysis checks use synthetic images
with known ledges, gaps, slopes, noise, gradients and softened edges; the native
collision fixture checks generation replacement and independent source limits.
Mapping checks compare composed pixels for the supported placements, crop
large centered images, and cover alpha blending and window occlusion.
The fighter fixture runs the actual map-collision phase and native collision
solver, while recording entry to the unchanged knockout routine. It covers
fast downward steps, unsupported falls, attached throws, immediate release,
native throw-release position repair, hitlag and ordinary stages. This avoids
requiring character animation or death assets and does not simulate every
character-specific throw.
The pixel checks cover row padding, RGBA/BGRA
conversion, alpha preservation and resolution scaling.
They do not exercise Windows' compositor, a physical controller or a real
Melee match. The package manifest records the compiled files and their DLL
imports so the supplied build can be identified precisely.

The key implementation areas are:

- `src/pc/desktop_game.c`: match options, arena mapping, window and wallpaper collision,
  fixed camera and HUD.
- `src/pc/desktop_fighter.c`: desktop floor collision and bottom-knockout ordering.
- `src/pc/desktop_host.cpp`: tray, settings shortcut and mode lifecycle.
- `extern/aurora/lib/desktop.cpp`: Win32 overlay and asynchronous frame transfer.
- `extern/aurora/lib/desktop_windows.cpp`: read-only window bounds and visibility.
- `extern/aurora/lib/desktop_window_geometry.hpp`: exposed top-edge spans.
- `extern/aurora/lib/desktop_wallpaper.cpp`: local wallpaper decoding and placement.
- `extern/aurora/lib/desktop_wallpaper_geometry.hpp`: generated ledge extraction.
- `extern/aurora/lib/desktop_wallpaper_mapping.hpp`: placement and terrain visibility.
- `extern/aurora/lib/desktop_pixels.hpp`: alpha-preserving pixel conversion.
- `tools/windows/desktop_launcher.cpp`: native match setup UI.
- `tools/windows/desktop_update.cpp`: background startup release checks.
- `tools/package_desktop_windows.py`: dependency checks and portable packaging.
- `tools/package_desktop_installer.py`: single-file Windows installer packaging.
- `.github/workflows/desktop-release.yml`: tested GitHub release publication.

The added code carries GPL-3.0-or-later notices. Existing upstream licensing
and attribution are retained; decompiled game code is not relicensed. See the
included `licenses` directory and upstream license notes for the components.

## Sources and credits

- Base port: <https://github.com/999sian/melee-pc>
- Original decompilation: <https://github.com/doldecomp/melee>
- Desktop concept preview:
  <https://www.reddit.com/r/decomps/comments/1xh73ww/preview_i_made_the_melee_decomp_run_as_a/>
- SDL: <https://github.com/libsdl-org/SDL>
- Dawn/WebGPU: <https://dawn.googlesource.com/dawn>
- Win32 layered windows:
  <https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows>
- Visible window bounds and physical pixel coordinates:
  <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect>
- DWM window attributes and cloaked-window filtering:
  <https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute>
- Windows wallpaper query and placement:
  <https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-idesktopwallpaper>
- Windows Imaging Component scaling:
  <https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale>

Super Smash Bros. Melee and its characters belong to their respective owners.
This project is independent of Nintendo, the Slippi team, and the author of the
macOS Desktop Melee preview.
