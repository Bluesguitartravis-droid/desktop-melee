# Windows 0.1.8: four fighters and display selection

## Match setup
Choose **Fighters: 2, 3 or 4**. The extra rows are disabled when not used.
Select **Watch CPUs**, **Player 1 vs. CPUs**, **Local players**, or a custom
mix. Each active row selects its character, costume, and either CPU or a
specific controller port (1-4). Human fighters cannot share a port. CPU
level and stock count apply to all CPU fighters / all fighters respectively.
These are free-for-all stock matches; team battles are not added here.

All active fighters receive separate initial and respawn positions and a HUD
entry. Duplicate characters choose different available costumes. Existing
saved two-fighter settings and disc selection are retained on upgrade.

## Display selection
Select a connected display using **Display**, or leave **Primary display
(automatic)** selected. **Refresh** rescans after connecting a display.
The selection is saved by Windows display interface ID when available, with
a display-device-name fallback, rather than a list index. A disconnected
saved display is shown as unavailable; choose another instead of silently
moving the match to an unintended screen.

The chosen display supplies the arena bounds, floor, window-platform
coordinates and wallpaper image/layout. A visible bottom taskbar is used
when available; otherwise there is a floor 48 physical pixels above the
bottom (the existing fallback). Screens left of or above the primary use
negative Windows coordinates, not a different physics origin. Mixed-DPI
screens use physical pixels for both geometry and overlay placement.

Choose one display at a time. Spanning monitors and separate simultaneous
matches are not implemented. Stop the match before selecting another display.
Changing the display layout or taskbar during a match exits the overlay so
rendering and collision cannot silently drift apart; relaunch afterward.
Wallpaper Tile/Span across multiple monitors remains unsupported.

## Validation and remaining device checks
ROM-free native tests exercise all 28 two/three/four-slot human/CPU combinations,
controller port uniqueness, repeated-character costumes, separate initial and
respawn points, HUD positions, actual four-CPU target selection and CPU
reinitialization. Geometry tests cover secondary displays, negative origins,
portrait layouts, wrong-monitor taskbars, hidden taskbars and fallback floors.
The existing collision, fighter and updater regressions remain required.

A cross-compiled Windows build is not a live Windows gameplay test. Check a
four-CPU match on the second display, then controller play and window dragging.
Controller enumeration, live multi-monitor/DPI changes and sustained game
performance still require testing on Windows. No disc data is included.

Only the Windows launcher exposes these new settings. Existing Android and
Linux published binaries are unchanged; porting their controls/UI is deferred.
