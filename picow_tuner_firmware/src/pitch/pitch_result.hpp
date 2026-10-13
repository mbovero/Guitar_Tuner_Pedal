#pragma once

// An estimated pitch and its validity; returned by an estimator
struct PitchResult
{
    bool valid = false;     // Whether this estimate is valid and should be used for tuning feedback
    float freq_hz = 0.0f;   // Latest estimated frequency
};