#pragma once

#include <cstddef>
#include <cstdint>

namespace idle_image
{
    inline constexpr uint16_t width = 240;
    inline constexpr uint16_t height = 240;
    inline constexpr size_t byte_count = static_cast<size_t>(width) * height * 2u;

    // RGB565 pixels in display wire order, high byte first
    extern const uint8_t pixels[byte_count];
}
