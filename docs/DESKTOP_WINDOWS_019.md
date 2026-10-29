# Windows 0.1.9: all-monitor arena

## Span mode
Enable **Span match across all monitors** before starting a match. Desktop
Melee builds one transparent overlay over the Windows virtual desktop and
uses that same coordinate space for camera bounds, blast zones, the main floor
and open-window platforms.

Single-monitor selection remains the default. Turning span mode on disables the
individual display picker until it is turned off again. The choice is saved in
launcher settings.

## Floor and monitor layout
The combined arena uses the union of every connected monitor, including screens
with negative Windows coordinates. One common horizontal floor is chosen inside
the vertical band shared by all connected displays. This keeps the floor visible
on every screen when monitors have different heights or small vertical offsets.

If the connected displays have no shared horizontal band at all, the launcher
refuses to start an all-monitor match and asks for a different layout instead
of creating an arena whose floor would be invisible on one screen.

The overlay may include transparent virtual-desktop space between irregularly
arranged monitors. Fighters can exist in that coordinate space even though no
physical panel displays those pixels, so conventional side-by-side monitor
layouts are the best first test.

## Platforms and wallpaper
Normal visible application windows on every monitor contribute their exposed
top edges as moving one-way platforms. Moving, resizing, minimizing and closing
those windows continues to update collision across the combined arena.

Wallpaper terrain is disabled in all-monitor mode in 0.1.9. Windows can use a
different image and placement on each monitor, and treating those files as one
image would produce incorrect collision. Single-monitor mode retains the
existing wallpaper terrain feature.

## Rendering and performance
The renderer preserves the full virtual-desktop aspect ratio while using the
existing bounded GPU readback resolution, then scales the transparent result
over the combined desktop. A multi-monitor arena is wider and can require a
larger layered-window bitmap, so live frame rate and input latency need testing
on actual multi-monitor Windows systems.

## Verification
ROM-free geometry tests cover equal-height side-by-side displays, negative
origins, mismatched monitor heights and layouts with no vertical overlap.
The existing 2-4 fighter, CPU targeting, collision, fighter and updater tests
remain part of the Windows validation build.

No disc image or extracted game data is included.
