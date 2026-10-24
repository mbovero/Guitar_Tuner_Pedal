#include "lcd_st7789.hpp"
#include "pico/stdlib.h"
#include "debug.hpp"

int main()
{
    stdio_init_all();

    lcd::init();

    while (true)
    {
        DEBUG_PRINT("RED\n");
        lcd::fill(0xF800);
        sleep_ms(1000);

        DEBUG_PRINT("GREEN\n");
        lcd::fill(0x07E0);
        sleep_ms(1000);

        DEBUG_PRINT("BLUE\n");
        lcd::fill(0x001F);
        sleep_ms(1000);
    }
}