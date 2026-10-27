#include "tuner_display.hpp"
#include "lcd_st7789.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint16_t background_color = 0x0000;
    constexpr uint16_t text_color = 0xFFFF;
    constexpr uint16_t frequency_color = 0xC618;
    constexpr uint16_t in_tune_color = 0x07E0;
    constexpr uint16_t warning_color = 0xF800;

    constexpr int screen_width = static_cast<int>(lcd::width);

    // x location of the center of the screen
    constexpr int center_x = screen_width / 2;
    // Thickness of clipping warning border in pixeks
    constexpr int border_thickness = 5;

    /* Variables for drawing error in cents */
    // Keep dynamic clearing inside the left and right edges of the clipping border
    constexpr int content_width = screen_width - 2 * border_thickness;
    constexpr int cents_scale = 3;
    constexpr int cents_y_offset = 15;
    // Each glyph by default is 6 pixels wide, this is then scaled up
    constexpr int cents_advance = 6 * cents_scale;
    // Reserve five number cells, one space, and one fixed cents symbol
    // Center the seven-cell layout, then advance six cells to locate the symbol
    // This keeps the symbol fixed when the numeric string gains or loses a digit
    constexpr int cents_symbol_x =
        (screen_width - 7 * cents_advance) / 2 + 6 * cents_advance;

    /* Variables for drawing tuning feedback bar */
    constexpr int feedback_y_offset = 50;
    constexpr int feedback_height = 60;
    constexpr int feedback_center_half_width = 4;
    // Leave five blank pixels between a fully extended bar and the clipping border
    constexpr int feedback_max_width = 106;
    // An exponent of one half applies a square-root response to the error magnitude
    constexpr float feedback_exponent = 0.5f;

    /* Variables for drawing target pitch */
    constexpr int pitch_scale = 8;
    constexpr int pitch_y_offset = 127;
    // The note uses pitch_scale, the accidental uses pitch_scale - 1, and the octave uses pitch_scale - 2
    // Sum their character-cell widths, subtract half from center_x, then apply the existing pitch_scale offset
    constexpr int pitch_x_offset =
        (center_x - (6 * (pitch_scale + pitch_scale - 2)) / 2) + pitch_scale;
    constexpr int sharp_x_offset =
        (center_x - (6 * (pitch_scale + pitch_scale - 1 + pitch_scale - 2)) / 2) + pitch_scale;

    /* Variables for drawing the current and target frequencies */
    constexpr int frequency_scale = 2;
    constexpr int frequency_y_offset = 200;

    /* Cached variables to prevent flickering */
    // Displayed values are cached so unchanged regions do not need to be redrawn (causing flickering)
    // Formatted strings are cached rather than raw floats, since different measurements can display identical text
    bool feedback_drawn = false;
    bool prev_valid = false;
    bool prev_clipping = false;

    char prev_cents_text[8] = {};
    int prev_feedback_width = 0;    // A signed bar width encodes both its length and which side of the center marker it occupies

    int prev_midi_note = -1;
    bool prev_tuned = false;

    char prev_current_text[10] = {};
    char prev_target_text[10] = {};
}

// Initialize the panel, draw static elements, and reset the display cache
void tuner_display::init()
{
    lcd::init();
    lcd::fill(background_color);

    // Draw the bar separating current and target frequency once
    lcd::fill_rect(center_x - 2, frequency_y_offset, 4, 30, frequency_color);

    // Draw the center of the tuning feedback bar once
    lcd::fill_rect(
        center_x - feedback_center_half_width,
        feedback_y_offset,
        2 * feedback_center_half_width,
        feedback_height,
        text_color);

    // The font maps ASCII 'c' to the cents symbol, drawn once per initialization
    lcd::draw_char(
        cents_symbol_x, cents_y_offset, 'c',
        text_color, background_color, cents_scale);

    // Reset cached values
    feedback_drawn = false;
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
    /* Update the clipping indicator, even when no pitch is available */
    if (prev_clipping != clipping)
    {
        lcd::draw_rect_outline(
            0, 0, lcd::width, lcd::height, border_thickness,
            clipping ? warning_color : background_color);
    }
    prev_clipping = clipping;

    // Redraw idle feedback only on the first update or when entering idle
    // Clipping is handled before this return so it can change while pitch feedback remains invalid
    if (feedback_drawn && !prev_valid && !feedback.valid)
    {
        return;
    }

    // When feedback is invalid, display placeholder information; when valid, extract feedback to display
    const bool force_redraw = !feedback_drawn || prev_valid != feedback.valid;

    const float cents_error = feedback.valid ? feedback.cents_error : 0.0f;
    const char* note_name = feedback.valid ? feedback.note_name : "?";
    const int octave = feedback.valid ? feedback.octave : 0;
    const int midi_note = feedback.valid ? feedback.midi_note : -1;
    const bool tuned = feedback.valid && feedback.status == TuningStatus::InTune;
    const float current_frequency = feedback.valid ? feedback.measured_freq_hz : 0.0f;
    const float target_frequency = feedback.valid ? feedback.target_freq_hz : 0.0f;

    /* Draw the cents error without redrawing the fixed cents symbol */
    // Multiplying by ten moves the first fractional digit into the integer position
    // Round there, then divide by ten to obtain a value rounded to the nearest tenth
    float displayed_cents = std::round(cents_error * 10.0f) / 10.0f;

    if (displayed_cents == 0.0f)
    {
        // Floating-point zero can retain a negative sign after rounding
        // Assign positive zero so formatting does not show a minus sign for a displayed zero
        displayed_cents = 0.0f;
    }

    char cents_text[8];

    // Always print a sign and one fractional digit, reserving at least four character positions
    std::snprintf(cents_text, sizeof(cents_text), "%+4.1f", displayed_cents);

    if (force_redraw || std::strcmp(prev_cents_text, cents_text) != 0)
    {
        // strlen counts number characters only, so add two cells for the space and fixed symbol
        const int cents_width =
        (static_cast<int>(std::strlen(cents_text)) + 2) * cents_advance;
        // Subtracting the total cell width from the screen width and halving centers that layout
        const int cents_x = (screen_width - cents_width) / 2;

        // Erase only the old cents error, preserving the cents symbol
        lcd::fill_rect(
            border_thickness, cents_y_offset,
            cents_symbol_x - border_thickness, 8 * cents_scale,
            background_color);
        // Draw new cents error
        lcd::draw_string(
            cents_x, cents_y_offset, cents_text,
            text_color, background_color, cents_scale);
    }

    /* Draw the nonlinear tuning feedback bar */
    // Map -50 through +50 cents to -1 through +1 and clamp larger errors to the endpoints
    const float normalized = std::clamp(cents_error / 50.0f, -1.0f, 1.0f);

    // Take the absolute value before the fractional power to avoid a square root of a negative value
    // For magnitudes between zero and one, the square root is larger than the original magnitude
    // This keeps moderate errors visually prominent while still reaching zero at perfect tuning
    const float magnitude = std::pow(std::abs(normalized), feedback_exponent);

    // Restore the original error sign, scale to the maximum pixel width, and round to a whole pixel
    // Negative widths represent the left bar, positive widths the right bar, and zero draws neither
    const int feedback_width = static_cast<int>(std::lround(
        feedback_max_width * std::copysign(magnitude, normalized)));

    if (prev_feedback_width != feedback_width)
    {
        // The red bars begin outside the fixed white center marker
        // A left bar occupies [left_edge - width, left_edge)
        // A right bar occupies [right_edge, right_edge + width)
        // These ranges include the first coordinate and exclude the last coordinate
        const int left_edge = center_x - feedback_center_half_width;
        const int right_edge = center_x + feedback_center_half_width;

        // Split each signed width into nonnegative lengths for the two sides
        // Negation converts a negative width into a left-side length, while max discards the inactive side
        const int prev_left_width = std::max(-prev_feedback_width, 0);
        const int left_width = std::max(-feedback_width, 0);
        const int prev_right_width = std::max(prev_feedback_width, 0);
        const int right_width = std::max(feedback_width, 0);

        // Update only the strip added to or removed from the left bar
        if (prev_left_width != left_width)
        {
            // The larger left-side length reaches farther left and locates the changed strip's start
            // The absolute length difference is exactly the number of columns to add or erase
            const int changed_x = left_edge - std::max(prev_left_width, left_width);
            const int changed_width = std::abs(left_width - prev_left_width);

            const uint16_t color =
                left_width > prev_left_width ? warning_color : background_color;

            // Growth paints new columns red and shrinkage replaces removed columns with the background
            lcd::fill_rect(
                changed_x, feedback_y_offset, changed_width, feedback_height, color);
        }

        // Update only the strip added to or removed from the right bar
        if (prev_right_width != right_width)
        {
            // The smaller right-side length ends where the changed strip begins
            // For growth this is the old outer edge, and for shrinkage it is the new outer edge
            const int changed_x = right_edge + std::min(prev_right_width, right_width);
            const int changed_width = std::abs(right_width - prev_right_width);

            const uint16_t color =
                right_width > prev_right_width ? warning_color : background_color;

            lcd::fill_rect(
                changed_x, feedback_y_offset, changed_width, feedback_height, color);
        }

        // On a direction change, one side shrinks to zero while the other grows from zero
        // Each side updates only its changed strip, leaving the center marker untouched
    }

    /* Draw the target note when its pitch or in-tune color changes */
    if (force_redraw || prev_midi_note != midi_note || prev_tuned != tuned)
    {
        // Decimal digit character codes are consecutive, so adding the octave to '0' selects its glyph
        // This layout expects a single-digit octave from zero through nine
        const char octave_char = static_cast<char>(octave + '0');

        const uint16_t pitch_color = tuned ? in_tune_color : text_color;

        // Clear old target pitch
        lcd::fill_rect(
            border_thickness, pitch_y_offset, content_width, 68, background_color);

        // Draw a question mark if feedback is invalid
        if (!feedback.valid)
        {
            lcd::draw_char(
                center_x - 3 * pitch_scale, pitch_y_offset, '?',
                text_color, background_color, pitch_scale);
        }
        else if (note_name[1] == '\0')  // A natural note has only one character before the string terminator
        {
            // Draw target pitch
            lcd::draw_char(
                pitch_x_offset, pitch_y_offset, note_name[0],
                pitch_color, background_color, pitch_scale);
            // Draw octave
            lcd::draw_char(
                pitch_x_offset + 6 * pitch_scale, pitch_y_offset + 20, octave_char,
                pitch_color, background_color, pitch_scale - 2);
        }
        else    // Otherwise, the target pitch has an accidental
        {
            // Draw target pitch
            lcd::draw_char(
                sharp_x_offset, pitch_y_offset, note_name[0],
                pitch_color, background_color, pitch_scale);
            // Draw accidental
            lcd::draw_char(
                sharp_x_offset + 6 * pitch_scale, pitch_y_offset + 5, note_name[1],
                pitch_color, background_color, pitch_scale - 1);
            // Draw octave
            lcd::draw_char(
                sharp_x_offset + 6 * pitch_scale + 6 * (pitch_scale - 1),
                pitch_y_offset + 20, octave_char,
                pitch_color, background_color, pitch_scale - 2);
        }
    }

    /* Draw measured and target frequencies only when their text changes */
    char current_text[10];
    char target_text[10];
    // Convert current and target frequency floats to strings
    std::snprintf(current_text, sizeof(current_text), "%.1f Hz", current_frequency);
    std::snprintf(target_text, sizeof(target_text), "%.1f Hz", target_frequency);

    // Right-align the measured frequency within a nine-character layout
    // Each missing character shifts its starting position right by one scaled character cell
    const int current_x_spacing = 6 * frequency_scale *
        (9 - static_cast<int>(std::strlen(current_text)));
    
    // Only redraw current frequency when it has changed
    if (force_redraw || std::strcmp(prev_current_text, current_text) != 0)
    {
        // Clear old current frequency
        lcd::fill_rect(
            border_thickness, frequency_y_offset,
            center_x - 3 - border_thickness, 30, background_color);
        // Draw new current frequency
        lcd::draw_string(
            current_x_spacing + 5, frequency_y_offset + 8, current_text,
            frequency_color, background_color, frequency_scale);
    }
    // Only redraw target frequency when it has changed
    if (force_redraw || std::strcmp(prev_target_text, target_text) != 0)
    {
        // Clear old target frequency
        lcd::fill_rect(
            center_x + 3, frequency_y_offset,
            center_x - 3 - border_thickness, 30, background_color);
        // Draw new target frequency
        lcd::draw_string(
            center_x + 2 + 5, frequency_y_offset + 8, target_text,
            frequency_color, background_color, frequency_scale);
    }

    /* Cache the values represented by the current screen */
    // Update the cache after drawing so the next call compares against what is actually displayed
    feedback_drawn = true;
    prev_valid = feedback.valid;
    std::snprintf(prev_cents_text, sizeof(prev_cents_text), "%s", cents_text);
    prev_feedback_width = feedback_width;
    prev_midi_note = midi_note;
    prev_tuned = tuned;
    std::snprintf(prev_current_text, sizeof(prev_current_text), "%s", current_text);
    std::snprintf(prev_target_text, sizeof(prev_target_text), "%s", target_text);
}