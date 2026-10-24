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

    void draw_char(
        uint16_t x,
        uint16_t y,
        char character,
        uint16_t foreground,
        uint16_t background,
        uint8_t scale = 1);
    void draw_string(
        uint16_t x,
        uint16_t y,
        const char* text,
        uint16_t foreground,
        uint16_t background,
        uint8_t scale = 1);
}