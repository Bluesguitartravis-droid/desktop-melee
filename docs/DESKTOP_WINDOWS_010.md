# Windows 0.1.10: staggered all-monitor layouts

Version 0.1.10 extends the **Span match across all monitors** option for monitor
arrangements that do not share one common horizontal band.

The lowest connected monitor supplies the normal Melee main floor. Every other
monitor contributes a broad one-way floor along its own bottom/taskbar edge,
using the same dynamic collision system as desktop window platforms. This lets
fighters occupy upper displays and fall toward lower displays without requiring
all screens to line up vertically.

The combined transparent overlay still covers the Windows virtual desktop.
Open application windows on every display remain moving platforms. The main
floor is limited to the physical width of the lowest monitor, avoiding an
invisible floor underneath virtual-desktop gaps.

Parallel displays whose floors are exactly level receive an internal one-pixel
offset for the extra dynamic floor. This is visually negligible and lets the
native main floor and additional monitor floor coexist safely.

Wallpaper terrain remains disabled while spanning all monitors. Single-monitor
wallpaper terrain is unchanged.

This remains experimental on arbitrary physical layouts. The build includes
geometry regression cases for side-by-side, negative-coordinate, vertically
disconnected and five-monitor staggered arrangements. Live testing on the
actual Windows layout is still required.
