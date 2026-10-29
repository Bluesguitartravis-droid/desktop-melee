# Desktop Melee changes

## 0.1.10 — 2026-10-29

### Staggered all-monitor layouts

- All-monitor mode no longer requires every display to share one horizontal band.
- The lowest physical monitor supplies the native main floor while each higher
  monitor contributes a wide one-way floor at its own taskbar/lower edge.
- Side-by-side displays at the same height remain usable; additional equal-height
  floors are lifted by one physical pixel internally so they can coexist with
  Melee's native main-floor collision.
- This supports mixed stacks such as a lower center screen beneath a row of
  upper monitors and an offset auxiliary display.
- The native main floor is limited to the actual lowest monitor width instead
  of spanning invisible virtual-desktop gaps.

## 0.1.9 — 2026-10-29

### All-monitor arena

- Added an opt-in **Span match across all monitors** toggle to the Windows launcher.
- The overlay, camera, blast zones, main floor and window-platform coordinate system use the full Windows virtual desktop in span mode.
- Negative monitor origins and mixed monitor sizes are supported. A single common floor is placed inside the vertical band shared by every monitor; layouts with no shared horizontal band are rejected rather than creating an invisible arena.
- Window tops from every monitor participate in the same match. Single-monitor selection remains the default.
- Wallpaper terrain is intentionally disabled in span mode for this release; per-monitor wallpaper composition needs a separate mapping pass.

### Verification

- Added span-geometry tests for side-by-side monitors, negative origins, mismatched heights and non-overlapping vertical layouts.
- Existing two-to-four-player, CPU targeting, collision, fighter and updater regression suites remain required.

## 0.1.7 Linux preview 1 — 2026-10-28

First native Linux x86_64 preview, with Steam Deck X11 Desktop Mode as a
target. It is a separate prerelease; Windows stable remains at 0.1.6.

### Linux desktop and launcher

- Transparent, click-through X11 overlay with a bottom-panel floor and
  moving platforms on exposed window tops.
- Native Linux launcher for choosing the disc, fighters, CPU watch mode,
  stocks and window platforms. It offers explicit windowed play when the
  desktop overlay is unavailable.
- Wayland and Gamescope sessions are detected. The initial desktop backend
  requires a real composited X11 desktop; Xwayland alone is insufficient.
- Steam Deck guidance covers the X11 desktop-session selection, Steam Input
  and a native Non-Steam Game entry without Proton.
- Linux wallpaper terrain is not enabled in this first preview.

### Packaging and updates

- One AppImage bundles the application and its runtime dependencies; a
  complete portable tarball is also available. Installation does not need
  administrator access or changes to SteamOS's read-only system.
- The launcher checks for compatible Linux previews and stable Linux
  releases in the background, without selecting a Windows asset.
- Preferences, saved games and caches live in stable per-user locations so
  replacing the AppImage preserves them.
- The Linux workflow produces exact-commit source, SHA-256 checksums and
  build manifests. Linux preview tags are excluded from the Windows release
  workflow and do not become the Windows stable update.

### Verification and limits

Regression checks exercise native collision, update metadata and release
provenance. The X11 backend is exercised in a virtual composited display;
a software-Vulkan smoke check covers GPU readback, overlay pause/resume
and shutdown. Disc-free smoke checks run the real Linux executables. Steam Deck
hardware, actual game performance, controller mappings, battery use and
suspend/resume still require device testing.

## 0.1.6 — 2026-10-28

First GitHub release of this Windows desktop fork.

### Installation and updates

- One Windows installer bundles the launcher, game engine, required DLLs and
  resources. A portable ZIP remains available separately.
- The launcher checks this repository's releases in the background at startup.
  A newer stable version offers the Windows installer download. Offline and
  failed checks leave the game playable.
- Updates are installed explicitly by running the new installer after closing
  the game. Launcher settings and the installed `user` folder are preserved.
- Release builds include matching source and SHA-256 checksums. No disc image
  or extracted game assets are distributed.

### Desktop geometry and game behavior

- Open window tops provide moving one-way platforms, with occlusion handling
  for overlapping windows. Moving, resizing, minimizing and closing windows
  update their collision.
- Wallpaper terrain follows continuous foreground outlines as connected
  slopes. Clear hills no longer depend on having a flat hilltop.
- Native floor collision resolves before a desktop bottom knockout check,
  avoiding premature KOs during fast descents through the thin taskbar area.
- Attached regular throw animations receive a narrow supporting-floor guard;
  native release collision still handles the landing.
- CPU targeting adapts to the small space between the taskbar and the lower
  knockout boundary. Watch mode uses ordinary VS CPU combat behavior.
- Desktop GPU readback stays on its render worker and preserves transparency.

### Verification and limits

Automated tests cover native collision and CPU behavior, wallpaper extraction,
geometry clipping, update selection and release-package consistency. The
Windows binaries and installer are cross-built. Live installer operation,
network checks, character-specific throws and controller behavior still need
confirmation on Windows. Wallpaper recognition remains experimental.
