// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../stream_settings.hpp"
#include "../hardware_video_contract.hpp"
#include "../../demo_renderer.hpp"
namespace opennow::gpu {
// All GL and VideoOut operations belong to the main presentation thread.
bool initialize() noexcept;
bool available() noexcept;
void shutdown() noexcept;
bool profileAvailable(StreamProfile) noexcept;
StreamProfile bestProfile() noexcept;
bool drawVideo(const video::NativeSurface&,const video::NativeMode&) noexcept;
void drawOverlay(const std::uint32_t*) noexcept;
void drawInterface(const std::uint32_t*) noexcept;
bool swap() noexcept;
bool takeVideoDrawn() noexcept;
const char* outputLabel() noexcept;
}
