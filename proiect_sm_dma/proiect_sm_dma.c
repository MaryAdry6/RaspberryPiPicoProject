#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#define SAMPLE_RATE     44100
#define ADC_PIN         26
#define PWM_PIN         22
#define BUFFER_SIZE     256

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


static inline uint16_t process_sample(uint16_t sample) {
    return sample;
}

void dma_irq_handler() {
    if (dma_channel_get_irq0_status(adc_dma_chan)) {
        dma_channel_acknowledge_irq0(adc_dma_chan);

        uint16_t *src = (dma_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        uint16_t *dst = (dma_write_buffer == 0) ? out_buffer_0 : out_buffer_1;

        for (int i = 0; i < BUFFER_SIZE; i++) {
            dst[i] = process_sample(src[i]);
        }

        play_buffer = dma_write_buffer;
        new_buffer_ready = true;

        dma_write_buffer ^= 1;
        uint16_t *next = (dma_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        dma_channel_set_write_addr(adc_dma_chan, next, true);
    }
}

bool timer_callback(struct repeating_timer *t) {
    if (!new_buffer_ready) return true;

    uint16_t *buf = (play_buffer == 0) ? out_buffer_0 : out_buffer_1;

    
    uint8_t pwm_val = buf[play_index] >> 4; // 12-bit to 8-bit
    pwm_set_gpio_level(PWM_PIN, pwm_val);

    play_index++;
    if (play_index >= BUFFER_SIZE) {
        play_index = 0;
        new_buffer_ready = false;
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
    pwm_config_set_wrap(&pwm_cfg, 255);
    pwm_init(pwm_slice, &pwm_cfg, true);
    pwm_set_gpio_level(PWM_PIN, 128);

}

void timer_init()
{
    // Negative period = use exact microsecond interval from start of last call
    add_repeating_timer_us(
        -(1000000 / SAMPLE_RATE),   // ~22us per sample @ 44100Hz
        timer_callback,
        NULL,
        &timer
    );
}

int main() {
    stdio_init_all();
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
    }

    return 0;
}