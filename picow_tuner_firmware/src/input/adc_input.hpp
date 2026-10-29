#pragma once

#include "sample_frame.hpp"

void initialize_sample_input();

void start_sample_input();

void stop_sample_input();

bool try_get_sample_frame(SampleFrame& frame);

uint32_t dropped_sample_frames();