#pragma once

#include <cstdint>

namespace lcd
{
    constexpr uint16_t width = 240;
    constexpr uint16_t height = 240;

    void init();

    void fill(uint16_t color);
    void fill_rect(
        uint16_t x,
        uint16_t y,
        uint16_t rect_width,
        uint16_t rect_height,
        uint16_t color);
    void draw_pixel(uint16_t x, uint16_t y, uint16_t color);
    void draw_rect_outline(
        uint16_t x, 
        uint16_t y,
        uint16_t rect_width,
        uint16_t rect_height,
        uint16_t thickness,
        uint16_t color);
}