/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
/* Called after disc selection and before the first game scene. */
bool pc_desktop_host_init(struct SDL_Window* window);
/* Between aurora_update() and aurora_begin_frame(), never during recording. */
void pc_desktop_host_tick(void);
void pc_desktop_host_shutdown(void);
#ifdef __cplusplus
}
#endif
