#include "lcd_st7789.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "pico/stdlib.h"
#include "hardware/spi.h"

namespace
{
    // Pico W GPIO pins chosen for the waveshare 1.54" LCD module
    constexpr uint pin_clk = 18;    // SPI clock GPIO #     (CLK)
    constexpr uint pin_mosi = 19;   // SPI MOSI GPIO #      (DIN)
    constexpr uint pin_cs = 17;     // Chip select GPIO #   (CS)
    constexpr uint pin_dc = 16;     // Command/Data GPIO #  (DC)
    constexpr uint pin_rst = 20;    // Reset GPIO #         (RST)

    constexpr uint spi_baudrate = 1'000'000;

    // Helper function that configures the provided GPIO pin as an output with the specified initial_level
    void init_output(uint pin, bool initial_level)
    {
        gpio_init(pin);
        gpio_put(pin, initial_level);
        gpio_set_dir(pin, GPIO_OUT);
    }

    void init_bus()
    {
        init_output(pin_cs, true);  // Deselect display initially
        init_output(pin_dc, true);  // Initially sending pixel data
        init_output(pin_rst, true); // Don't reset

        // Initialize SPI peripheral
        spi_init(spi0, spi_baudrate);
        spi_set_format(
            spi0,           // Use first SPI peripheral
            8,              // Transfer 8 bits per SPI word
            SPI_CPOL_0,     // Clock rests low
            SPI_CPHA_0,     // Receiver samples on the rising CLK edge
            SPI_MSB_FIRST); // Transmit high bit first, low bit last
    }
}