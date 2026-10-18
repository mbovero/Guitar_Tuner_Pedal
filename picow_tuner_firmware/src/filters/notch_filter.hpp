#pragma once

class NotchFilter
{
public:
    bool configure(
        double sampling_rate,
        double frequency_hz,
        double quality_factor);

    void reset(double initial_value = 0.0);

    double process(double input);

private:
    // 5 coefficients that determine filter response
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    // History values
    double input_1 = 0.0;   // Previous input
    double input_2 = 0.0;   // Input before previous input
    double output_1 = 0.0;  // Previous output
    double output_2 = 0.0;  // Output before previous output
};