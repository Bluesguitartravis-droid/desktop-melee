/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

namespace aurora::desktop::wallpaper {
/* UI thread. Stop and join the optional image worker before Aurora logging
 * and its other shared process state are shut down. */
void shutdown() noexcept;
}
