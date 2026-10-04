/*
 * ps5-native-app-boilerplate - CPU VideoOut demonstration implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides the bounded drawing surface and PS5 presentation loop used by the
 * editable starter application.
 */

#include "demo_renderer.hpp"
#ifdef OPENNOW_GPU
#include "stream/native/gpu_presenter.hpp"
#endif
#include <sys/event.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen();
    int open(const char *path, int flags, ...);
    long read(int descriptor, void *buffer, std::size_t size);
    int close(int descriptor);
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    bool ps5ObserveOwnedAllocation(const void *address) noexcept;
}

extern "C" {
int sceKernelDeleteEqueue(struct kevent*);
int sceVideoOutClose(int);
int sceKernelCreateEqueue(struct kevent**,const char*);
int sceKernelWaitEqueue(struct kevent*,struct kevent*,int,int*,unsigned*);
int sceVideoOutAddFlipEvent(struct kevent*,int,void*);
}
namespace ps5::demo
{
namespace
{
std::atomic_bool stopRequested{false};
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
constexpr std::size_t frame_bytes = 0x1000000;
constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

struct Glyph
{
    char character;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 95> glyphs{{
    {' ', {0, 0, 0, 0, 0, 0, 0}},        {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},   {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},   {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}}, {'G', {14, 17, 16, 23, 17, 17, 14}},
    {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},     {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 21, 19, 17, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},  {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'a', {0,0,14,1,15,17,15}},
    {'b', {16,16,30,17,17,17,30}},
    {'c', {0,0,15,16,16,16,15}},
    {'d', {1,1,15,17,17,17,15}},
    {'e', {0,0,14,17,31,16,14}},
    {'f', {6,8,28,8,8,8,8}},
    {'g', {0,15,17,17,15,1,14}},
    {'h', {16,16,30,17,17,17,17}},
    {'i', {4,0,12,4,4,4,14}},
    {'j', {2,0,6,2,2,18,12}},
    {'k', {16,16,18,20,24,20,18}},
    {'l', {12,4,4,4,4,4,14}},
    {'m', {0,0,26,21,21,21,21}},
    {'n', {0,0,30,17,17,17,17}},
    {'o', {0,0,14,17,17,17,14}},
    {'p', {0,0,30,17,30,16,16}},
    {'q', {0,0,15,17,15,1,1}},
    {'r', {0,0,22,25,16,16,16}},
    {'s', {0,0,15,16,14,1,30}},
    {'t', {8,8,28,8,8,9,6}},
    {'u', {0,0,17,17,17,19,13}},
    {'v', {0,0,17,17,17,10,4}},
    {'w', {0,0,17,17,21,21,10}},
    {'x', {0,0,17,10,4,10,17}},
    {'y', {0,0,17,17,15,1,14}},
    {'z', {0,0,31,2,4,8,31}},
    {':', {0,4,4,0,4,4,0}},
    {'/', {1,2,2,4,8,8,16}},
    {'.', {0,0,0,0,0,6,6}},
    {'-', {0,0,0,31,0,0,0}},
    {'_', {0,0,0,0,0,0,31}},
    {'?', {14,17,1,2,4,0,4}},
    {'=', {0,31,0,31,0,0,0}},
    {'&', {12,18,20,8,21,18,13}},
    {'(', {2,4,8,8,8,4,2}},
    {')', {8,4,2,2,2,4,8}},
    {'+', {0,4,4,31,4,4,0}},
    {'|', {4,4,4,4,4,4,4}},
    {'!', {4,4,4,4,4,0,4}},
    {'@', {14,17,23,21,23,16,14}},
    {'#', {10,31,10,10,31,10,0}},
    {'$', {4,15,20,14,5,30,4}},
    {'%', {25,25,2,4,8,19,19}},
    {'^', {4,10,17,0,0,0,0}},
    {'*', {0,21,14,31,14,21,0}},
    {'[', {14,8,8,8,8,8,14}},
    {']', {14,2,2,2,2,2,14}},
    {'{', {6,8,8,16,8,8,6}},
    {'}', {12,2,2,1,2,2,12}},
    {'\\', {16,8,8,4,2,2,1}},
    {';', {0,4,4,0,4,4,8}},
    {'\'', {4,4,8,0,0,0,0}},
    {'"', {10,10,0,0,0,0,0}},
    {',', {0,0,0,0,4,4,8}},
    {'<', {2,4,8,16,8,4,2}},
    {'>', {8,4,2,1,2,4,8}},
    {'`', {8,4,2,0,0,0,0}},
    {'~', {0,0,9,22,0,0,0}},
}};

class File final
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }

    ~File()
    {
        reset();
    }

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    File(File &&other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)}
    {
    }

    File &operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptor_ >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    void reset() noexcept
    {
        if (descriptor_ >= 0)
        {
            (void)close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class LifetimeProbe final
{
  public:
    explicit LifetimeProbe(bool &destroyed) noexcept : destroyed_{&destroyed}
    {
    }

    ~LifetimeProbe()
    {
        *destroyed_ = true;
    }

    LifetimeProbe(const LifetimeProbe &) = delete;
    LifetimeProbe &operator=(const LifetimeProbe &) = delete;

  private:
    bool *destroyed_;
};

NotificationRequest notification{};

void copy_message(std::span<char> destination, std::string_view source) noexcept
{
    if (destination.empty())
        return;

    const std::size_t count =
        source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = source[index];
    destination[count] = '\0';
}

void notify(std::string_view message) noexcept
{
    copy_message(std::span{notification.message}, message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[nodiscard]] bool verify_unique_ownership() noexcept
{
    bool destroyed = false;
    {
        std::unique_ptr<LifetimeProbe> probe{new (std::nothrow_t{}) LifetimeProbe{destroyed}};
        if (!ps5ObserveOwnedAllocation(probe.get()))
            return false;
    }
    return destroyed;
}

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::span<const std::uint8_t, 7> glyph_rows(char character) noexcept
{
    for (const auto &glyph : glyphs)
    {
        if (glyph.character == character)
            return glyph.rows;
    }
    return glyphs.front().rows;
}

[[nodiscard]] constexpr std::size_t tiled_byte_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    const std::uint32_t block_index = (y >> 7) * blocks_per_row + (x >> 7);

    return (static_cast<std::size_t>(block_index) << 16) + offset;
}

#ifdef OPENNOW_GPU
thread_local bool linear_canvas=false;
#else
constexpr bool linear_canvas=false;
#endif

void put_pixel_unchecked(std::uint32_t *pixels, unsigned x, unsigned y, Color color) noexcept
{
    auto *bytes = reinterpret_cast<std::uint8_t *>(pixels);
    *reinterpret_cast<std::uint32_t *>(bytes + (linear_canvas ? std::size_t(y*frame_width+x)*4 : tiled_byte_offset(x, y))) =
        static_cast<std::uint32_t>(color);
}

void fill_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width, unsigned height,
               Color color) noexcept
{
    if (x >= frame_width || y >= frame_height)
        return;

    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned bottom = height > frame_height - y ? frame_height : y + height;
    for (unsigned row = y; row < bottom; ++row)
    {
        for (unsigned column = x; column < right; ++column)
            put_pixel_unchecked(pixels, column, row, color);
    }
}

void fill_circle(std::uint32_t *pixels, unsigned center_x, unsigned center_y, unsigned radius,
                 Color color) noexcept
{
    const int signed_radius = static_cast<int>(radius);
    for (int y = -signed_radius; y <= signed_radius; ++y)
    {
        for (int x = -signed_radius; x <= signed_radius; ++x)
        {
            if (x * x + y * y <= signed_radius * signed_radius)
            {
                const int pixel_x = static_cast<int>(center_x) + x;
                const int pixel_y = static_cast<int>(center_y) + y;
                if (pixel_x >= 0 && pixel_y >= 0)
                {
                    const auto bounded_x = static_cast<unsigned>(pixel_x);
                    const auto bounded_y = static_cast<unsigned>(pixel_y);
                    if (bounded_x < frame_width && bounded_y < frame_height)
                        put_pixel_unchecked(pixels, bounded_x, bounded_y, color);
                }
            }
        }
    }
}

void fill_triangle(std::uint32_t *pixels, unsigned center_x, unsigned top, unsigned half_width,
                   unsigned height, Color color) noexcept
{
    if (height == 0 || center_x >= frame_width)
        return;

    for (unsigned row = 0; row < height; ++row)
    {
        const unsigned half = row * half_width / height;
        const unsigned left = half > center_x ? 0 : center_x - half;
        const unsigned right = half >= frame_width - center_x ? frame_width : center_x + half + 1;
        fill_rect(pixels, left, top + row, right - left, 1, color);
    }
}

void draw_text(std::uint32_t *pixels, unsigned x, unsigned y, std::string_view value,
               unsigned scale, Color color) noexcept
{
    for (const char character : value)
    {
        const auto rows = glyph_rows(character);
        for (unsigned row = 0; row < rows.size(); ++row)
        {
            for (unsigned column = 0; column < 5; ++column)
            {
                if ((rows[row] & (1U << (4 - column))) != 0)
                    fill_rect(pixels, x + column * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
        if (x >= frame_width)
            return;
    }
}

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

void Canvas::image(unsigned x,unsigned y,unsigned width,unsigned height,const std::uint32_t* data) noexcept
{
    if(!data||x>=frame_width||y>=frame_height||width>frame_width-x||height>frame_height-y)return;
    for(unsigned row=0;row<height;++row)for(unsigned col=0;col<width;++col)
        put_pixel_unchecked(pixels_,x+col,y+row,static_cast<Color>(data[row*width+col]));
}

void Canvas::beginOverlay() noexcept
{
#ifndef OPENNOW_HOST_PREVIEW
    if(opennow::gpu::available())clear(static_cast<Color>(0));
#endif
}
void Canvas::endOverlay() noexcept
{
#ifndef OPENNOW_HOST_PREVIEW
    if(opennow::gpu::available())opennow::gpu::drawOverlay(pixels_);
#endif
}

void Canvas::clear(Color color) noexcept
{
    fill_rect(pixels_, 0, 0, frame_width, frame_height, color);
}

void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                       Color color) noexcept
{
    fill_rect(pixels_, x, y, width, height, color);
}

void Canvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    fill_circle(pixels_, center_x, center_y, radius, color);
}

void Canvas::triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                      Color color) noexcept
{
    fill_triangle(pixels_, center_x, top, half_width, height, color);
}

void Canvas::button(unsigned x, unsigned y, Button button, unsigned size, Color color) noexcept
{
    if (size < 16) return;
    // Draw every symbol in the same square, using thin, antialiased outlines.
    const auto line = [](float px, float py, float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay;
        const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) /
                                  (dx * dx + dy * dy), 0.0f, 1.0f);
        return std::hypot(px - ax - t * dx, py - ay - t * dy);
    };
    for (unsigned row = 0; row < size && y + row < frame_height; ++row)
    {
        for (unsigned column = 0; column < size && x + column < frame_width; ++column)
        {
            unsigned coverage = 0;
            for (unsigned sy = 0; sy < 4; ++sy)
            for (unsigned sx = 0; sx < 4; ++sx)
            {
                const float px = (column + (sx + 0.5f) / 4) / size;
                const float py = (row + (sy + 0.5f) / 4) / size;
                bool ink = false;
                switch (button)
                {
                case Button::cross:
                    ink = std::min(line(px, py, .23f, .23f, .77f, .77f),
                                   line(px, py, .77f, .23f, .23f, .77f)) < .035f;
                    break;
                case Button::circle:
                    ink = std::abs(std::hypot(px - .5f, py - .5f) - .32f) < .035f;
                    break;
                case Button::square:
                    ink = std::abs(std::max(std::abs(px - .5f), std::abs(py - .5f)) - .30f) < .035f;
                    break;
                case Button::triangle:
                    ink = std::min({line(px, py, .5f, .16f, .85f, .78f),
                                    line(px, py, .85f, .78f, .15f, .78f),
                                    line(px, py, .15f, .78f, .5f, .16f)}) < .035f;
                    break;
                case Button::up:
                case Button::down:
                {
                    const float qy = button == Button::up ? py : 1 - py;
                    ink = std::min({line(px, qy, .25f, .46f, .5f, .21f),
                                    line(px, qy, .5f, .21f, .75f, .46f),
                                    line(px, qy, .5f, .21f, .5f, .79f)}) < .035f;
                    break;
                }
                case Button::dpad:
                    // Four distinct direction arrows, matching the controller's D-pad.
                    for (unsigned turn = 0; turn < 4; ++turn)
                    {
                        const float qx = turn == 0 ? px : turn == 1 ? py : turn == 2 ? 1-px : 1-py;
                        const float qy = turn == 0 ? py : turn == 1 ? 1-px : turn == 2 ? 1-py : px;
                        ink |= std::min({line(qx,qy,.38f,.28f,.5f,.16f),
                                         line(qx,qy,.5f,.16f,.62f,.28f),
                                         line(qx,qy,.5f,.16f,.5f,.37f)}) < .027f;
                    }
                    break;
                case Button::options:
                    for (float yy : {.30f, .50f, .70f})
                        ink |= line(px, py, .25f, yy, .75f, yy) < .03f;
                    break;
                case Button::touchpad:
                {
                    const float qx=std::max(std::abs(px-.5f)-.35f,0.0f);
                    const float qy=std::max(std::abs(py-.5f)-.16f,0.0f);
                    ink=std::abs(std::hypot(qx,qy)-.04f)<.025f;
                    for(float xx:{.3f,.5f,.7f})for(float yy:{.43f,.57f})
                        ink|=std::hypot(px-xx,py-yy)<.02f;
                    break;
                }
                case Button::right_stick:
                    ink=std::abs(std::hypot(px-.5f,py-.5f)-.34f)<.03f;
                    break;
                case Button::l2:
                case Button::r2:
                case Button::l1:
                case Button::r1:
                {
                    const float qx = std::max(std::abs(px-.5f)-.32f, 0.0f);
                    const float qy = std::max(std::abs(py-.5f)-.20f, 0.0f);
                    ink = std::abs(std::hypot(qx,qy)-.09f) < .025f;
                    break;
                }
                }
                coverage += ink;
            }
            if (!coverage) continue;
            const auto offset = linear_canvas ? std::size_t((y+row)*frame_width+x+column)*4 :
                                                tiled_byte_offset(x+column, y+row);
            const auto base = *reinterpret_cast<const std::uint32_t *>(
                reinterpret_cast<const std::uint8_t *>(pixels_) + offset);
            std::uint32_t blended = 0xff000000;
            for (unsigned shift : {0U, 8U, 16U})
            {
                const unsigned foreground = (static_cast<std::uint32_t>(color) >> shift) & 255U;
                const unsigned background = (base >> shift) & 255U;
                blended |= ((foreground * coverage + background * (16-coverage) + 8) / 16) << shift;
            }
            put_pixel_unchecked(pixels_, x+column, y+row, static_cast<Color>(blended));
        }
    }
    if (button == Button::l1 || button == Button::r1 || button == Button::l2 || button == Button::r2 || button == Button::right_stick)
    {
        const unsigned scale = size / 20;
        const std::string_view label=button==Button::l1?"L1":button==Button::r1?"R1":button==Button::l2?"L2":button==Button::r2?"R2":"R";
        draw_text(pixels_, x + (size - (label.size()*6-1)*scale)/2, y + (size - 7*scale)/2,
                  label, scale, color);
    }
}

void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale,
                  Color color) noexcept
{
    draw_text(pixels_, x, y, value, scale, color);
}

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept
{
    copy_message(destination, fallback);
    File descriptor{open(path, 0)};
    if (!descriptor.valid() || destination.empty())
        return;

    const long count = read(descriptor.get(), destination.data(), destination.size() - 1);
    if (count <= 0)
        return;

    std::size_t length = static_cast<std::size_t>(count);
    while (length > 0 && (destination[length - 1] == '\r' || destination[length - 1] == '\n'))
        --length;
    destination[length] = '\0';
}

void requestStop() noexcept {stopRequested.store(true);}

void run(DrawScene draw, std::string_view ready_message) noexcept
{
#ifdef OPENNOW_HOST_PREVIEW
    auto* pixels=static_cast<std::uint32_t*>(std::calloc(frame_bytes,1));
    if (!pixels) std::exit(1);
    Canvas canvas(pixels); draw(canvas);
    auto* file=std::fopen("build/preview.ppm","wb");
    if (!file) std::exit(1);
    std::fprintf(file,"P6\n%u %u\n255\n",frame_width,frame_height);
    for (unsigned i=0;i<frame_width*frame_height;++i) {
        const auto pixel=pixels[tiled_byte_offset(i%frame_width,i/frame_width)/4];
        const unsigned char rgb[]={static_cast<unsigned char>(pixel),static_cast<unsigned char>(pixel>>8),static_cast<unsigned char>(pixel>>16)};
        std::fwrite(rgb,1,3,file);
    }
    std::fclose(file); std::free(pixels); std::exit(0);
#else
#ifdef OPENNOW_GPU
    if(opennow::gpu::available()) {
        linear_canvas=true;
        auto* pixels=static_cast<std::uint32_t*>(std::calloc(frame_width*frame_height,4));
        if(!pixels)halt("OpenNOW: UI allocation failed");
        Canvas canvas(pixels);
        (void)sceSystemServiceHideSplashScreen();
        while(!stopRequested.load()) {
            if(draw(canvas)) {
                if(!opennow::gpu::takeVideoDrawn())opennow::gpu::drawInterface(pixels);
                if(!opennow::gpu::swap())halt("OpenNOW: GPU presentation failed");
            } else sceKernelUsleep(1000);
        }
        std::free(pixels);
        return;
    }
#endif
    if (!verify_unique_ownership())
        halt("OpenNOW: unique ownership failed");
    if (draw == nullptr)
        halt("OpenNOW: scene callback missing");

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("OpenNOW: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("OpenNOW: insufficient direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("OpenNOW: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("OpenNOW: direct-memory mapping failed");

    Canvas first{static_cast<std::uint32_t *>(mapped)};
    auto *second_frame = static_cast<std::uint8_t *>(mapped) + frame_bytes;
    Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};
    draw(first);
    // The second buffer is drawn before its first presentation.
    flush_range(mapped, memory_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {second_frame, nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, frame_width,
                                   frame_height, 0, 0, 0);

    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("OpenNOW: buffer registration failed");
    struct kevent* queue=nullptr;
    if (sceKernelCreateEqueue(&queue,"opennow-flip")<0 || sceVideoOutAddFlipEvent(queue,video,nullptr)<0)
        halt("OpenNOW: flip event setup failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("OpenNOW: initial flip failed");

    (void)sceVideoOutWaitVblank(video);
    notify(ready_message);

    // Wait for completion before recycling a displayed buffer.
    unsigned index=0;
    std::int64_t serial=1;
    while(!stopRequested.load()) {
        struct kevent event{}; int count=0; unsigned timeout=2000000;
        const int waited=sceKernelWaitEqueue(queue,&event,1,&count,&timeout);
        if(stopRequested.load())break;
        if(waited<0||count!=1)halt("OpenNOW: flip completion timed out");
        index^=1;
        auto& canvas=index ? second : first;
        // Poll promptly so a 60 FPS stream does not wait another 16 ms for a flip.
        while (!stopRequested.load()&&!draw(canvas)) sceKernelUsleep(2000);
        if(stopRequested.load())break;
        flush_range(index ? second_frame : mapped,frame_bytes);
        if (sceVideoOutSubmitFlip(video,index,1,++serial)<0)
            halt("OpenNOW: flip failed");
    }
    (void)sceKernelDeleteEqueue(queue);
    (void)sceVideoOutClose(video);
#endif
}
} // namespace ps5::demo
