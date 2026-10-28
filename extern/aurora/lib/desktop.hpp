#pragma once

#include <aurora/desktop.h>
#include <aurora/aurora.h>

#ifdef AURORA_ENABLE_GX
#include "webgpu/wgpu.hpp"
#include <functional>
#endif

namespace aurora::desktop {
bool active() noexcept;
bool window_size(AuroraWindowSize& size) noexcept;
/* Separate XCB shortcut events cannot wake SDL's blocking event wait. */
bool needs_event_poll() noexcept;
/* UI thread only. Consumes the newest completed GPU readback, never waits
 * for the GPU. The overlay always lets mouse input pass through. */
void pump() noexcept;
void shutdown() noexcept;
#ifdef AURORA_ENABLE_GX
/* Render worker only. The target has the configured (capped) output size. */
wgpu::Texture frame_target();
std::function<void()> record_readback(const wgpu::CommandEncoder& encoder,
                                      const wgpu::Texture& texture);
#endif
} // namespace aurora::desktop
