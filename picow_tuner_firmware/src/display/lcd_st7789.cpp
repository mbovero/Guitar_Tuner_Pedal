#include "lcd_st7789.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <algorithm>

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

    constexpr uint spi_baudrate = 16'000'000;

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
        // Configure SPI pins
        gpio_set_function(pin_clk, GPIO_FUNC_SPI);
        gpio_set_function(pin_mosi, GPIO_FUNC_SPI);
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
        gpio_put(pin_dc, true);    // Indicate that data is being sent
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

    // Tells the LCD controler what rectangular region to update, then instructs it to begin writing pixel memory
    void set_window(
        uint16_t x_start,
        uint16_t y_start,
        uint16_t x_end,
        uint16_t y_end)
    {
        // Set column range
        write_register(0x2A, {
            static_cast<uint8_t>(x_start >> 8),     // Write upper bits
            static_cast<uint8_t>(x_start & 0xFF),   // Write lower bits
            static_cast<uint8_t>(x_end >> 8),
            static_cast<uint8_t>(x_end & 0xFF)
        });

        // Set row range
        write_register(0x2B, {
            static_cast<uint8_t>(y_start >> 8),
            static_cast<uint8_t>(y_start & 0xFF),
            static_cast<uint8_t>(y_end >> 8),
            static_cast<uint8_t>(y_end & 0xFF)
        });

        // Begin writing pixel memory
        write_command(0x2C);
    }
}

// Initializes bus to communicate with LCD driver, then resets and configures the panel
void lcd::init()
{
    init_bus();
    reset_panel();
    configure_panel();
}

/*
 * Fills a rectangle of the specified size at the given location with the RGB565 color.
 * x and y specify the location of the top left corner of the rectangle.
 */
void lcd::fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t rect_width,
    uint16_t rect_height,
    uint16_t color)
{
    // Return early if parameters are invalid
    if (x >= width || y >= height ||
        rect_width <= 0 || rect_height <= 0)
    {
        return;
    }
    
    // Clip rectangles to fit screen
    rect_width = std::min<uint16_t>(rect_width, width - x);
    rect_height = std::min<uint16_t>(rect_height, height - y);

    // Create an array of bytes to hold RGB565 values
    uint8_t row[width * 2];

    // Isolate RGB565 color bytes
    const uint8_t high_byte = static_cast<uint8_t>(color >> 8);
    const uint8_t low_byte = static_cast<uint8_t>(color & 0xFF);

    // Fill array to rectangle width with color
    for (uint16_t column = 0; column < rect_width; ++column)
    {
        row[2 * column] = high_byte;
        row[2 * column + 1] = low_byte;
    }

    // Calculate pixel location of the bottom right corner of the rectangle
    const uint16_t x_end = static_cast<uint16_t>(x + rect_width - 1);
    const uint16_t y_end = static_cast<uint16_t>(y + rect_height - 1);

    // Tell display controller to only update the rectangle's area
    set_window(x, y, x_end, y_end);

    gpio_put(pin_dc, true); // Tell controller data is being sent
    gpio_put(pin_cs, false);// Select LCD controller chip

    // Display rectangle row by row
    for (uint16_t row_index = 0; row_index < rect_height; ++row_index)
    {
        spi_write_blocking(spi0, row, rect_width * 2);
    }

    // Deselect LCD controller
    gpio_put(pin_cs, true);
}

// Fills the entire LCD display with the provided RGB565 color
void lcd::fill(uint16_t color)
{
    fill_rect(0, 0, width, height, color);
}
