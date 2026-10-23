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

    // Initializes GPIO pins and SPI peripheral to communicate with LCD controller
    void init_bus()
    {
        init_output(pin_cs, true);  // Deselect LDC controller chip initially
        init_output(pin_dc, true);  // Not sending commands initially
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

    // Sends the provided command to the LCD controller
    void write_command(uint8_t command)
    {
        gpio_put(pin_dc, false);    // Indicate that a command is being sent
        gpio_put(pin_cs, false);    // Select LCD controller chip

        spi_write_blocking(spi0, &command, 1);  // Send command via SPI peripheral

        gpio_put(pin_cs, true); // Deselect LCD controller chip
    }

    // Writes "count" bytes from the provided "data" array to the LCD controller
    void write_data(const uint8_t* data, size_t count)
    {
        gpio_put(pin_dc, false);    // Indicate that data is being sent
        gpio_put(pin_cs, false);    // Select LCD controller chip

        spi_write_blocking(spi0, data, count);  // Send command via SPI peripheral

        gpio_put(pin_cs, true); // Deselect LCD controller chip
    }

    // Sends provided command to LCD controller then writes provided parameters
    void write_register(uint8_t command, std::initializer_list<uint8_t> parameters)
    {
        write_command(command);
        write_data(parameters.begin(), parameters.size());
    }

    // Resets LCD controller
    void reset_panel()
    {
        // Release reset signal
        gpio_put(pin_rst, true);
        sleep_ms(100);
        // Assert reset signal
        gpio_put(pin_rst, false);
        sleep_ms(100);
        // Release
        gpio_put(pin_rst, true);
        sleep_ms(100);
    }

    // Configure's LCD controller's pixel format, orientation, and panel settings
    void configure_panel()
    {
        // Pixel format and orientation were chosen. Documentation on the commands can be found here:
        // https://www.waveshare.com/w/upload/a/ae/ST7789_Datasheet.pdf

        // Memory access direction: initial, unrotated configuration
        write_register(0x36, {0x00});

        // Pixel format: 16-bit RGB565 on the MCU interface
        write_register(0x3A, {0x05});

        // The remaining configurations were implemented referencing waveshare's driver:
        // https://github.com/waveshare/Pico_code/blob/main/c/lib/LCD/LCD_1in54.c

        // Porch timing.
        write_register(0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33});

        // Internal panel drive settings.
        write_register(0xB7, {0x35});       // Gate control
        write_register(0xBB, {0x19});       // VCOM
        write_register(0xC0, {0x2C});       // LCM control
        write_register(0xC2, {0x01});       // Enable VRH/VDV settings
        write_register(0xC3, {0x12});       // VRH
        write_register(0xC4, {0x20});       // VDV
        write_register(0xC6, {0x0F});       // Frame rate
        write_register(0xD0, {0xA4, 0xA1}); // Power control

        // Positive gamma curve.
        write_register(0xE0, {
            0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
            0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23});

        // Negative gamma curve.
        write_register(0xE1, {
            0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
            0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23});

        write_command(0x21); // Display inversion on

        write_command(0x11); // Leave sleep mode
        sleep_ms(120);

        write_command(0x29); // Display on
        sleep_ms(20);
    }
}

// Initializes bus to communicate with LCD driver, then resets and configures the panel
void lcd::init()
{
    init_bus();
    reset_panel();
    configure_panel();
}