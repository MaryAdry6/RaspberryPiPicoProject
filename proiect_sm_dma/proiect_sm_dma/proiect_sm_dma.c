#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#define ADC_PIN         26
#define PWM_PIN         22
#define BUFFER_SIZE     256
#define SAMPLE_RATE     50000

uint16_t adc_buffer_0[BUFFER_SIZE];
uint16_t adc_buffer_1[BUFFER_SIZE];

uint16_t out_buffer_0[BUFFER_SIZE];
uint16_t out_buffer_1[BUFFER_SIZE];

volatile uint8_t  dma_write_buffer  = 0;
volatile uint8_t  play_buffer       = 0;
volatile bool     new_buffer_ready  = false;

volatile uint32_t play_index = 0;

int adc_dma_chan;
int pwm_slice;
static struct repeating_timer timer;

// ==========================================
// --- DSP GLOBALS & BUFFERS ---
// ==========================================

typedef enum {
    FX_CLEAN,
    FX_DISTORTION,
    FX_DELAY,
    FX_REVERB
} EffectType;

// Change this variable to switch effects (e.g., inside your main loop via buttons)
volatile EffectType current_effect = FX_CLEAN; 

// --- 1. DISTORTION PARAMS ---
// dist_gain: Multiplies the signal. Higher = grainier/fuzzier. 25 is aggressive.
volatile int32_t dist_gain = 25; 
// dist_clip: The volume ceiling. (Max is 2047). 1000 gives a loud, heavily compressed fuzz.
volatile int32_t dist_clip = 1000;

// --- 2. DELAY PARAMS ---
// At 50kHz, 25,000 samples = 0.5 seconds of delay. (Takes ~50KB of RAM)
#define DELAY_MAX 25000 
uint16_t delay_buffer[DELAY_MAX];
volatile uint32_t delay_ptr = 0;
volatile uint32_t delay_depth = 20000; 

// --- 3. REVERB PARAMS ---
// Reverb uses multiple short delays (comb filters) at prime lengths 
// to prevent resonant ringing, using much less RAM.
#define REV_C1 4153
#define REV_C2 4789
#define REV_C3 5471
#define REV_C4 6263
#define REV_A1 1021
#define REV_A2 337

int16_t rev_c1_buf[REV_C1];
int16_t rev_c2_buf[REV_C2];
int16_t rev_c3_buf[REV_C3];
int16_t rev_c4_buf[REV_C4];
int16_t rev_a1_buf[REV_A1];
int16_t rev_a2_buf[REV_A2];

volatile uint32_t ptr_c1 = 0, ptr_c2 = 0, ptr_c3 = 0, ptr_c4 = 0;
volatile uint32_t ptr_a1 = 0, ptr_a2 = 0;


// ==========================================
// --- PROCESSING FUNCTION ---
// ==========================================

static inline uint16_t process_sample(uint16_t sample_in) {
    
    if (current_effect == FX_CLEAN) {
        return sample_in;
    }
    
    // --- DISTORTION ---
    if (current_effect == FX_DISTORTION) {
        // 1. Auto-Center the DC Bias (Solves the sudden cut-off)
        // This acts as an ultra-slow tracker that finds your exact hardware bias.
        static int32_t dc_filter = 2048 << 14; 
        dc_filter += (sample_in - (dc_filter >> 14));
        int32_t true_dc = dc_filter >> 14;
        
        int32_t ac_sample = (int32_t)sample_in - true_dc;
        
        // 2. Input Anti-Feedback Filter (Solves the speeding-up buzz)
        // Smooths out high-frequency PWM noise before it can hit the gain stage.
        static int32_t lp_in = 0;
        lp_in = (lp_in * 3 + ac_sample) >> 2; 
        
        // 3. Apply Fuzz Gain
        int32_t distorted = lp_in * dist_gain;
        
        // 4. Hard Clipping (Squashing the wave)
        if (distorted > dist_clip) distorted = dist_clip;
        if (distorted < -dist_clip) distorted = -dist_clip;
        
        // 5. Cabinet Simulator / Output Filter
        // Rolls off the harsh, fizzy digital highs to sound like a real bass amp.
        static int32_t lp_out = 0;
        lp_out = (lp_out * 7 + distorted) >> 3; 

        // 6. Recenter to exactly 2048 for the clean PWM output
        int32_t out_sample = 2048 + lp_out;
        
        // Safety bounds
        if (out_sample > 4095) out_sample = 4095;
        if (out_sample < 0) out_sample = 0;
        
        return (uint16_t)out_sample;
    }
    
    // --- DELAY ---
    if (current_effect == FX_DELAY) {
        // Read old delayed sample
        uint16_t delayed_sample = delay_buffer[delay_ptr];
        
        // Overwrite with new input
        delay_buffer[delay_ptr] = sample_in;
        
        // Increment and wrap pointer
        delay_ptr++;
        if (delay_ptr >= delay_depth) delay_ptr = 0;
        
        // Mix 50/50: original + delayed
        return (sample_in + delayed_sample) >> 1; 
    }
    
    // --- REVERB ---
    if (current_effect == FX_REVERB) {
        // Strip the hardware DC bias immediately
        int32_t ac_sample = (int32_t)sample_in - 2048;
        
        // 1. High-Pass the input for the reverb tank (CRITICAL FOR BASS)
        // We separate the heavy low-end so only the mids/highs get reverberated.
        // This acts as a leaky integrator (low-pass), which we subtract from the original signal.
        static int32_t lp_bass = 0;
        lp_bass = (lp_bass * 15 + ac_sample) >> 4; 
        int32_t rev_input = ac_sample - lp_bass; // The "shimmer" without the low-end mud
        
        // 2. Read from the 4 Parallel Comb Filters
        int32_t read_c1 = rev_c1_buf[ptr_c1];
        int32_t read_c2 = rev_c2_buf[ptr_c2];
        int32_t read_c3 = rev_c3_buf[ptr_c3];
        int32_t read_c4 = rev_c4_buf[ptr_c4];
        
        // 3. High-Frequency Damping (Simulates air absorbing sound)
        static int32_t damp1 = 0, damp2 = 0, damp3 = 0, damp4 = 0;
        damp1 = (damp1 * 3 + read_c1) >> 2;
        damp2 = (damp2 * 3 + read_c2) >> 2;
        damp3 = (damp3 * 3 + read_c3) >> 2;
        damp4 = (damp4 * 3 + read_c4) >> 2;
        
        // 4. Write back with Feedback
        // Using ((x * 7) >> 3) is mathematically smoother for integer math than (x - (x >> 3)).
        // We scale the input down (>> 2) to give the feedback loop headroom and prevent clipping.
        int32_t in_scaled = rev_input >> 2;
        rev_c1_buf[ptr_c1] = in_scaled + ((damp1 * 7) >> 3);
        rev_c2_buf[ptr_c2] = in_scaled + ((damp2 * 7) >> 3);
        rev_c3_buf[ptr_c3] = in_scaled + ((damp3 * 7) >> 3);
        rev_c4_buf[ptr_c4] = in_scaled + ((damp4 * 7) >> 3);
        
        ptr_c1 = (ptr_c1 + 1) % REV_C1;
        ptr_c2 = (ptr_c2 + 1) % REV_C2;
        ptr_c3 = (ptr_c3 + 1) % REV_C3;
        ptr_c4 = (ptr_c4 + 1) % REV_C4;
        
        // 5. Mix the Comb Filters down into a single signal
        int32_t comb_out = (read_c1 + read_c2 + read_c3 + read_c4) >> 2;
        
        // 6. All-Pass Filter 1
        int32_t read_a1 = rev_a1_buf[ptr_a1];
        int32_t new_a1 = comb_out + (read_a1 >> 1);
        rev_a1_buf[ptr_a1] = new_a1;
        int32_t out_a1 = read_a1 - (new_a1 >> 1);
        ptr_a1 = (ptr_a1 + 1) % REV_A1;
        
        // 7. All-Pass Filter 2
        int32_t read_a2 = rev_a2_buf[ptr_a2];
        int32_t new_a2 = out_a1 + (read_a2 >> 1);
        rev_a2_buf[ptr_a2] = new_a2;
        int32_t out_a2 = read_a2 - (new_a2 >> 1);
        ptr_a2 = (ptr_a2 + 1) % REV_A2;
        
        // 8. Final Mix
        // 100% Dry Input (keeps your bass punchy) + 50% Wet Reverb Tail (out_a2 >> 1)
        int32_t mixed = ac_sample + (out_a2 >> 1);
        
        // 9. Restore the hardware DC bias so the PWM reads it correctly
        mixed += 2048; 
        
        // 10. Hardware Bounds Safety Net
        if (mixed > 4095) mixed = 4095;
        if (mixed < 0) mixed = 0;
        
        return (uint16_t)mixed;
    }
    
    return sample_in;
}

// ==========================================
// --- INTERRUPTS & PERIPHERALS ---
// ==========================================

void dma_irq_handler() {
    if (dma_channel_get_irq0_status(adc_dma_chan)) {
        dma_channel_acknowledge_irq0(adc_dma_chan);

        uint16_t *src = (dma_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        uint16_t *dst = (dma_write_buffer == 0) ? out_buffer_0 : out_buffer_1;

        // Process the samples in the background
        for (int i = 0; i < BUFFER_SIZE; i++) {
            dst[i] = process_sample(src[i]);
        }

        // Instantly switch the playback buffer. 
        // The timer will immediately start reading the new data on its next tick.
        play_buffer = dma_write_buffer;

        dma_write_buffer ^= 1;
        uint16_t *next = (dma_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        dma_channel_set_write_addr(adc_dma_chan, next, true);
    }
}

bool timer_callback(struct repeating_timer *t) {
    uint16_t *buf = (play_buffer == 0) ? out_buffer_0 : out_buffer_1;

    uint32_t pwm_val = (buf[play_index] * 2500) / 4095; 
    pwm_set_gpio_level(PWM_PIN, pwm_val);
    
    play_index++;
    if (play_index >= BUFFER_SIZE) {
        play_index = 0; // Just wrap around smoothly
    }
    return true;
}

void my_adc_init()
{
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0);

    adc_fifo_setup(
        true,
        true,
        1,
        false,
        false
    );

    adc_set_clkdiv(48000000.0f / SAMPLE_RATE - 1);
}

void dma_init()
{
    adc_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_cfg = dma_channel_get_default_config(adc_dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_ADC);

    dma_channel_configure(
        adc_dma_chan,
        &dma_cfg,
        adc_buffer_0,
        &adc_hw->fifo,
        BUFFER_SIZE,
        false
    );

    // DMA interrupt
    dma_channel_set_irq0_enabled(adc_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

}

void my_pwm_init()
{
    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(PWM_PIN);

    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_cfg, 1.0f);

    pwm_config_set_wrap(&pwm_cfg, 2500);
    pwm_init(pwm_slice, &pwm_cfg, true);
    pwm_set_gpio_level(PWM_PIN, 1250);
}

void timer_init()
{
    // Negative period = use exact microsecond interval from start of last call
    add_repeating_timer_us(
        -(1000000 / SAMPLE_RATE),   // ~20us per sample @ 50000Hz
        timer_callback,
        NULL,
        &timer
    );
}

int main() {
    stdio_init_all();

    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);
    
    sleep_ms(2000);
    printf("Bass Pedal Starting...\n");

    my_adc_init();
    dma_init();
    my_pwm_init();
    timer_init();
    
    // Start ADC + DMA
    adc_run(true);
    dma_channel_start(adc_dma_chan);

    printf("Running at %d Hz sample rate\n", SAMPLE_RATE);

    // main loop nu face nimic, totul e din intreruperi
    while (true) {
        tight_loop_contents();
        
        // TIP: You can read your GPIO buttons here in the future 
        // to change 'current_effect' (e.g., FX_CLEAN, FX_DELAY) 
        // or tweak 'dist_threshold' / 'delay_depth'.
    }

    return 0;
}