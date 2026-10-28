/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <melee/ft/forward.h>

/* Desktop's floor is only a taskbar's height above its bottom blast zone.
 * Give the native swept collision pass a chance to land before checking that
 * boundary. Other boundaries and ordinary stages retain their original order. */
void pc_desktop_check_blast_zones_before_map(Fighter_GObj* fighter);
void pc_desktop_check_blast_zones_after_map(Fighter_GObj* fighter);
