#include "lcd_st7789.hpp"
#include "pico/stdlib.h"
#include "debug.hpp"

int main()
{
    stdio_init_all();

    lcd::init();
    lcd::fill(0x0000);

    lcd::fill_rect(0, 0, 20, 20, 0xF800);
    lcd::fill_rect(220, 0, 20, 20, 0x07E0);
    lcd::fill_rect(0, 220, 20, 20, 0x001F);

    lcd::fill_rect(90, 90, 60, 60, 0xFFFF);

    // Intentionally extends beyond the screen.
    lcd::fill_rect(230, 230, 30, 30, 0xFFE0);

    while (true) {
        tight_loop_contents();
    }
}