#pragma once

#include "crossing_history.hpp"

// ADC target sampling rate
constexpr float sample_rate_hz = 16'000.0f;


void initialize_guitar_input();

bool try_get_crossing_event(CrossingEvent& event);
