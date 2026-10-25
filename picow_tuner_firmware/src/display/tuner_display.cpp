#include "tuner_display.hpp"
#include "lcd_st7789.hpp"
#include "debug.hpp"

#include "pico/stdlib.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

// Private static variables for tracking previous states to reduce screen flickering
namespace
{
    bool prev_valid = false;
    bool prev_clipping = false;

    char prev_cents_text[8] = {};
    int prev_feedback_width = 0;

    int prev_midi_note = -1;
    bool prev_tuned = false;

    char prev_current_text[10] = {};
    char prev_target_text[10] = {};
}

// Initializes the LCD and paints the entire display black
void tuner_display::init()
{
    // Initialize display
    lcd::init();
    // Reset screen to black
    lcd::fill(0x0000);

    // Reset cached values
    prev_valid = false;
    prev_clipping = false;
    prev_cents_text[0] = '\0';
    prev_feedback_width = 0;
    prev_midi_note = -1;
    prev_tuned = false;
    prev_current_text[0] = '\0';
    prev_target_text[0] = '\0';

}

void tuner_display::update(const TuningFeedback& feedback, bool clipping)
{
    // Initialize default feedback
    float cents_error = 0.0f;
    const char* note_name = "?";
    int octave = 0;
    float current_frequency = 0.0f;
    float target_frequency = 0.0f;
    
    // If feedback is invalid, display default feedback
    if (feedback.valid)
    {
        // Extract tuning feedback if valid
        cents_error = feedback.cents_error;
        note_name = feedback.note_name;
        octave = feedback.octave;
        current_frequency = feedback.measured_freq_hz;
        target_frequency = feedback.target_freq_hz;
    }
    else if (!prev_valid && !feedback.valid)    // If last update had invalid feedback, and feedback is still invalid
    {
        // Default feedback has already been displayed; do not re-display it
        return;
    }

    /* Draw error in cents */
    // Round the displayed value to tenths.
    float displayed_cents =
        std::round(cents_error * 10.0f) / 10.0f;
    // Prevent displaying "-0.0".
    if (displayed_cents == 0.0f) 
    {
        displayed_cents = 0.0f;
    }
    // Convert float error to string
    char cents_text[8];
    std::snprintf(cents_text, sizeof(cents_text), "%+4.1f c", displayed_cents);

    // Only redraw if cents text has changed
    if (std::strcmp(prev_cents_text, cents_text) != 0)
    {
        constexpr int cents_scale = 3;
        const int cents_width = static_cast<int>(std::strlen(cents_text) * 6 * cents_scale);
        const int cents_x = (static_cast<int>(lcd::width) - cents_width) / 2;
        // Clear old error text
        lcd::fill_rect(5, 15, 230, 24, 0x0000);
        // Draw cents error
        lcd::draw_string(cents_x, 15, cents_text, 0xFFFF, 0x0000, cents_scale);
    }


    /* Draw tuning feedback bar */
    // Scale feedback bar nonlinearly based on cents error 
    constexpr float exponent = 0.5f;
    float normalized = std::clamp(cents_error / 50.0f, -1.0f, 1.0f);
    float magnitude = std::pow(std::abs(normalized), exponent);
    int feedback_width = static_cast<int>(std::lround(110.0f * std::copysign(magnitude, normalized)));

    // Only redraw if feedback bar width has changed
    if (prev_feedback_width != feedback_width)
    {
        constexpr int feedback_y_offset = 50;
        constexpr int feedback_height = 60;
        // Clear old feedback
        lcd::fill_rect(5, feedback_y_offset, 230, feedback_height, 0x0000);
        // Draw feedback bar
        int feedback_x_offset = (feedback_width < 0) ? 120 + feedback_width : 120;
        lcd::fill_rect(feedback_x_offset, feedback_y_offset, std::abs(feedback_width), feedback_height, 0xf800);
        // Draw center bar
        lcd::fill_rect(120-4, feedback_y_offset, 8, feedback_height, 0xFFFF);
    }


    /* Draw target pitch */
    char octave_char = static_cast<char>(octave + '0');
    constexpr int pitch_scale = 8;
    constexpr int pitch_x_offset = (120 - (6*(pitch_scale + pitch_scale-2))/2) + pitch_scale;
    constexpr int sharp_x_offset = (120 - (6*(pitch_scale + pitch_scale-1 + pitch_scale-2))/2) + pitch_scale;
    constexpr int pitch_y_offset = 127;

    // Draw in green if in tune, otherwise draw in white
    const uint16_t pitch_color =
        feedback.status == TuningStatus::InTune
            ? 0x07E0
            : 0xFFFF;

    // Only redraw if pitch, octave, or tuned status has changed
    if (prev_midi_note != feedback.midi_note ||
        prev_tuned != (feedback.status == TuningStatus::InTune))
    {
        // Clear old pitch drawing
        lcd::fill_rect(5, pitch_y_offset, 230, 68, 0x0000);

        // Check target pitch and draw accordingly
        if (note_name[0] == '?')    // Draw invalid feedback as a question mark
        {
            // Draw question mark
            lcd::draw_char(120-3*8, 127, '?', 0xFFFF, 0x0000, 8);
        }
        else if (note_name[1] == '\0')  // Draw natural pitches centered
        {
            // Draw pitch
            lcd::draw_char(pitch_x_offset, pitch_y_offset, note_name[0], pitch_color, 0x0000, pitch_scale);
            // Draw octave
            lcd::draw_char(pitch_x_offset+(6*pitch_scale), pitch_y_offset+20, octave_char, pitch_color, 0x0000, pitch_scale-2);
        }
        else    // Draw accidentals centered
        {
            // Draw pitch
            lcd::draw_char(sharp_x_offset, pitch_y_offset, note_name[0], pitch_color, 0x0000, pitch_scale);
            // Draw sharp
            lcd::draw_char(sharp_x_offset+(6*pitch_scale), pitch_y_offset+5, note_name[1], pitch_color, 0x0000, pitch_scale-1);
            // Draw octave
            lcd::draw_char(sharp_x_offset+(6*pitch_scale)+(6*(pitch_scale-1)), pitch_y_offset+20, octave_char, pitch_color, 0x0000, pitch_scale-2);
        }
    }

    /* Draw current vs target frequency in Hz */
    // Convert current and target frequency to strings
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
    // Only redraw current frequency if the text has changed
    if (std::strcmp(prev_current_text, current_text) != 0)
    {
        // Clear old current frequency text
        lcd::fill_rect(5, frequency_y_offset, 120-3-5, 30, 0x0000);
        // Draw current frequency (Hz)
        lcd::draw_string(current_x_spacing+5, frequency_y_offset+8, current_text, 0xc618, 0x0000, 2);
    }
    // Only redraw target frequency if text has changed
    if (std::strcmp(prev_target_text, target_text) != 0)
    {
        // Clear old current frequency text
        lcd::fill_rect(120+3, frequency_y_offset, 120-3-5, 30, 0x0000);
        // Draw target frequency (Hz)
        lcd::draw_string(120+2+5, frequency_y_offset+8, target_text, 0xc618, 0x0000, 2);
    }

    // Paint divider between current and target frequency
    lcd::fill_rect(120-2, frequency_y_offset, 4, 30, 0xc618);    
    

    /* Draw clipping indicator */
    // Only redraw if clipping state has changed
    if(prev_clipping != clipping)
    {
        // If clipping, draw red outline, otherwise clear it
        lcd::draw_rect_outline(
            0, 0, 240, 240, 5,
            clipping ? 0xF800 : 0x0000);
    }


    /* Update cached values */
    prev_valid = feedback.valid;
    prev_clipping = clipping;

    std::snprintf(prev_cents_text, sizeof(prev_cents_text), "%s", cents_text);
    prev_feedback_width = feedback_width;

    prev_midi_note = feedback.midi_note;
    prev_tuned = feedback.status == TuningStatus::InTune;

    std::snprintf(prev_current_text, sizeof(prev_current_text), "%s", current_text);
    std::snprintf(prev_target_text, sizeof(prev_target_text), "%s", target_text);
}
