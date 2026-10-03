#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

void guitar_in_isr()
{

}

/*
 * Configure the ADC into free-running sampling mode to continuously read the guitar input signal, save 
 * conversions into an 8 sample FIFO buffer, and trigger an interrupt that is handled by guitar_in_isr() 
 * each time a new sample is recorded.
 */
void initialize_guitar_in()
{
    // Initialize ADC hardware
    adc_init();

    // Initialize GPIO26 for use as an ADC pin (high-impedance, no pullups, etc.)
    adc_gpio_init(26);
    // Select input 0 from the MUX (routed to GPIO26)
    adc_select_input(0);

    /* Setup an ADC FIFO that holds 8 samples; have each conversion written to the FIFO, disable DMA 
    requests, trigger FIFO IRQ each time a sample is received, enable error bit, and disable DMA byte 
    shifting */
    adc_fifo_setup(true, false, 1, true, false);

    // Set the ADC clock divisor to achieve the desired sampling rate
    constexpr float sample_rate_hz = 16'000.0f;
    adc_set_clkdiv((48'000'000.0f / sample_rate_hz) - 1.0f);

    // Register custom interrupt service routine for handling ADC samples
    irq_set_exclusive_handler(ADC_IRQ_FIFO, guitar_in_isr);
    // Enable ADC interrupts
    adc_irq_set_enabled(true);
    // Enable ADC FIFO interrupt on this core
    irq_set_enabled(ADC_IRQ_FIFO, true);

    // Enable free-running sampling mode
    adc_run(true);
}


int main()
{
    stdio_init_all();

    // Set up the ADC to intake the guitar signal
    initialize_guitar_in();

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
