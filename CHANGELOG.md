# Desktop Melee changes

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
