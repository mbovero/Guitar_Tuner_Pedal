#pragma once

#include <cstdint>

namespace lcd
{
    constexpr uint16_t width = 240;
    constexpr uint16_t height = 240;

    void init();
    void fill(uint16_t color);
}