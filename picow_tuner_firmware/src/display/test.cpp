#include "lcd_st7789.hpp"
#include "pico/stdlib.h"
#include "debug.hpp"
#include <cstdio>
#include <algorithm>
#include <cmath>

int main()
{
    stdio_init_all();

    lcd::init();
    lcd::fill(0x0000);  // Black background

    /* Draw error in cents */
    float cents_error = -1.0f;
    char cents_text[8];
    std::snprintf(cents_text, sizeof(cents_text), "%.1f c", cents_error);
    lcd::draw_string(120-60, 15, cents_text, 0xFFFF, 0x0000, 3);


    /* Draw tuning feedback bar */
    // Scale feedback bar nonlinearly based on cents error 
    constexpr float exponent = 0.5f;
    float normalized = std::clamp(cents_error / 50.0f, -1.0f, 1.0f);
    float magnitude = std::pow(std::abs(normalized), exponent);
    int feedback_width = static_cast<int>(std::lround(110.0f * std::copysign(magnitude, normalized)));

    constexpr int feedback_y_offset = 50;
    constexpr int feedback_height = 60;
    // Clear old feedback
    lcd::fill_rect(0, feedback_y_offset, 240, feedback_height, 0x0000);
    // Draw feedback bar
    int feedback_x_offset = (feedback_width < 0) ? 120 + feedback_width : 120;
    lcd::fill_rect(feedback_x_offset, feedback_y_offset, std::abs(feedback_width), feedback_height, 0xf800);
    // Draw center bar
    lcd::fill_rect(120-4, feedback_y_offset, 8, feedback_height, 0xFFFF);


    /* Draw target pitch */
    char note_name[] = "D#";
    int octave = 6;
    char octave_char = static_cast<char>(octave + '0');
    constexpr int pitch_scale = 8;
    constexpr int pitch_x_offset = (120 - (6*(pitch_scale + pitch_scale-2))/2) + pitch_scale;
    constexpr int sharp_x_offset = (120 - (6*(pitch_scale + pitch_scale-1 + pitch_scale-2))/2) + pitch_scale;
    constexpr int pitch_y_offset = 127;

    // Draw in green if in tune, otherwise draw in white
    uint16_t pitch_color = (std::abs(cents_error) <= 1.0f) ? 0x07e0 : 0xFFFF;

    // Draw target pitch differently if it is sharp
    if (note_name[1] == '\0')
    {
        // Draw pitch
        lcd::draw_char(pitch_x_offset, pitch_y_offset, note_name[0], pitch_color, 0x0000, pitch_scale);
        // Draw octave
        lcd::draw_char(pitch_x_offset+(6*pitch_scale), pitch_y_offset+20, octave_char, pitch_color, 0x0000, pitch_scale-2);
    }
    else
    {
        // Draw pitch
        lcd::draw_char(sharp_x_offset, pitch_y_offset, note_name[0], pitch_color, 0x0000, pitch_scale);
        // Draw sharp
        lcd::draw_char(sharp_x_offset+(6*pitch_scale), pitch_y_offset+5, note_name[1], pitch_color, 0x0000, pitch_scale-1);
        // Draw octave
        lcd::draw_char(sharp_x_offset+(6*pitch_scale)+(6*(pitch_scale-1)), pitch_y_offset+20, octave_char, pitch_color, 0x0000, pitch_scale-2);
    }

    /* Draw current vs target frequency in Hz */
    // Convert current and target frequency to strings
    float current_frequency = 1200.508;
    float target_frequency = 1244.508;
    char current_text[10];
    char target_text[10];
    std::snprintf(current_text, sizeof(current_text), "%.1f Hz", current_frequency);
    std::snprintf(target_text, sizeof(target_text), "%.1f Hz", target_frequency);

    // Calculate spacing required to align current frequency text
    int current_x_spacing = (6*2*9);
    char* ptr = current_text;
    while (*ptr != '\0')
    {
        current_x_spacing -= 6*2;
        ++ptr;
    }
    
    constexpr int frequency_y_offset = 200;
    // Clear old frequency text
    lcd::fill_rect(0, frequency_y_offset, 240, 30, 0x0000);
    // Draw current vs target frequency (Hz)
    lcd::draw_string(current_x_spacing+5, frequency_y_offset+8, current_text, 0xc618, 0x0000, 2);
    lcd::draw_string(120+2+5, frequency_y_offset+8, target_text, 0xc618, 0x0000, 2);
    // Paint divider between current and target frequency
    lcd::fill_rect(120-2, frequency_y_offset, 4, 30, 0xc618);
    

    /* Draw clipping indicator */
    bool clipping = false;
    // If input is clipping, draw red border around screen
    if (clipping)
    {
        lcd::draw_rect_outline(0, 0, 240, 240, 5, 0xf800);
    }

    while (true) 
    {
        tight_loop_contents();
        

    }
}