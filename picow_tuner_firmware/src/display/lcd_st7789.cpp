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


    // Contains a character and its corresponding 7 x 5 bitmap
    struct Glyph
    {
        char character;
        uint8_t rows[7];
    };

    constexpr Glyph font[] = 
    {
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},

        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0E}},

        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
        {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},

        {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
        {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}}
    };

    /*
     * Attempts to look up the glyph for the provided character. If one is found, the corresponding
     * bitmap is returned. Otherwise, the bitmap for '?' is returned.
     */
    const uint8_t* find_glyph(char character)
    {
        // Look up character glyph and return bit map
        for (const Glyph& glyph : font)
        {
            if (glyph.character == character)
            {
                return glyph.rows;
            }
        }

        // If character glyph does not exist, return '?' bitmap
        for (const Glyph& glyph : font)
        {
            // Look up '?' glyph
            if (glyph.character == '?')
            {
                return glyph.rows;
            }
        }

        return nullptr;
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
 * Fills a rectangle of the specified size at the given LCD location with a RGB565 color.
 * x and y specify the location of the top left corner of the rectangle. The top left of the
 * display is (0, 0).
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

// Sets the pixel at the specified LCD location to the provided RGB565 color
void lcd::draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    fill_rect(x, y, 1, 1, color);
}

void lcd::draw_rect_outline(
    uint16_t x, 
    uint16_t y,
    uint16_t rect_width,
    uint16_t rect_height,
    uint16_t thickness,
    uint16_t color)
{
    // Draw upper line
    fill_rect(x, y, rect_width, thickness, color);
    // Draw lower line
    fill_rect(x, (y + rect_height) - thickness, rect_width, thickness, color);
    // Draw left line without overlap
    fill_rect(x, y + thickness, thickness, rect_height - 2*thickness, color);
    // Draw right line without overlap
    fill_rect((x + rect_width) - thickness, y + thickness, thickness, rect_height - 2*thickness, color);
}

/*
 * Draws a scaled character with the specified foreground and background RGB565 colors at the provided
 * location on the LCD.
 * Drawing the background covers up old characters painted in the same position.
 */
void lcd::draw_char(
    uint16_t x,
    uint16_t y,
    char character,
    uint16_t foreground,
    uint16_t background,
    uint8_t scale)
{
    // Return early if parameters invalid
    if (scale == 0 || x >= width || y >= height)
    {
        return;
    }

    // Find the bitmap for the provided character
    const uint8_t* bitmap = find_glyph(character);
    // Return early if no bitmap found
    if (bitmap == nullptr)
    {
        return;
    }

    // Paint character cell background with extra space
    fill_rect(x, y, 6 * scale, 8 * scale, background);

    for (uint16_t row = 0; row < 7; ++row)
    {
        for (uint16_t column = 0; column < 5; ++column)
        {
            // Prepare mask based on column position to determine if current position is foreground
            const uint8_t mask = static_cast<uint8_t>(1u << (4 - column));

            // If current bitmap location holds 1, print scaled foreground pixel
            if ((bitmap[row] & mask) != 0)
            {
                fill_rect(
                    x + column * scale,
                    y + row * scale,
                    scale,
                    scale,
                    foreground);
            }
        }
    }
}


void lcd::draw_string(
    uint16_t x,
    uint16_t y,
    const char* text,
    uint16_t foreground,
    uint16_t background,
    uint8_t scale)
{
    // Return early if parameters are invalid
    if (text == nullptr ||
        scale == 0 ||
        x >= width || y >= height)
    {
        return;
    }

    // Initialize cursor to track x position of characters across display
    uint32_t cursor_x = x;
    // Calculate space between characters
    const uint32_t advance = 6u * scale;

    // Iterate through string and draw characters that fit on the display
    while (*text != '\0' && cursor_x < width)
    {
        draw_char(
            static_cast<uint16_t>(cursor_x),
            y,
            *text,
            foreground,
            background,
            scale);
        
        cursor_x += advance;    // Advance character spacing
        ++text; // Move to next character
    }
}
