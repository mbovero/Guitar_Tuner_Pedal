#include "lcd_st7789.hpp"
#include "pico/stdlib.h"
#include "debug.hpp"

int main()
{
    stdio_init_all();

    lcd::init();
    lcd::fill(0x0000);  // Black background

    lcd::fill_rect(0, 0, 20, 20, 0xF800);   // Red square top left
    lcd::fill_rect(220, 0, 20, 20, 0x07E0); // Green square top right
    lcd::fill_rect(0, 220, 20, 20, 0x001F); // Blue square bottom left

    lcd::fill_rect(90, 90, 60, 60, 0xFFFF); // White square at center
    lcd::draw_rect_outline(90, 90, 60, 60, 4, 0xfd80);   // Golden border

    // Purple diagonal pixels at center
    lcd::draw_pixel(119, 119, 0xa01f);
    lcd::draw_pixel(120, 120, 0xa01f);


    // Intentionally extends beyond the screen
    lcd::fill_rect(230, 230, 30, 30, 0xFFE0);   // Clipped yellow square bottom right

    // Draw aquamarine characters
    lcd::draw_char(30, 30, 'A', 0x07bf, 0x0000, 2);
    lcd::draw_char(70, 30, '#', 0x07bf, 0x0000, 4);
    lcd::draw_char(130, 30, '0', 0x07bf, 0x0000, 8);

    // Draw pink string
    lcd::draw_string(30, 180, "+ x -", 0xf81f, 0x0000, 4);

    while (true) 
    {
        tight_loop_contents();
    }
}