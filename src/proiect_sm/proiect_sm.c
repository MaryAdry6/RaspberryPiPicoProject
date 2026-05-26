#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#include "pico/multicore.h"

#include "generated_assets.h"

#define LED_WHITE       14
#define LED_RED         15
#define LED_BLUE        16
#define LED_YELLOW      17

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

// DSP GLOBALS & BUFFERS 

typedef enum {
    FX_CLEAN,
    FX_DISTORTION,
    FX_DELAY,
    FX_TREMOLO
} EffectType;

// Change this variable to switch effects (e.g., inside your main loop via buttons)
volatile EffectType current_effect = FX_CLEAN; 

// DISTORTION PARAMS
// dist_gain: Multiplies the signal. Higher = grainier/fuzzier. 25 is aggressive.
volatile int32_t dist_gain = 25; 
// dist_clip: The volume ceiling. (Max is 2047). 1000 gives a loud, heavily compressed fuzz.
volatile int32_t dist_clip = 1000;

// DELAY PARAMS
// At 50kHz, 25,000 samples = 0.5 seconds of delay. 
#define DELAY_MAX 25000 
uint16_t delay_buffer[DELAY_MAX];
volatile uint32_t delay_ptr = 0;
volatile uint32_t delay_depth = 20000; 


// TREMOLO PARAMS
volatile int32_t trem_rate = 50;  // 0-100: Controls speed (approx 1Hz to 10Hz)
volatile int32_t trem_depth = 80; // 0-100: Controls how deep the volume drop is

// PROCESSING FUNCTION 
static inline uint16_t __not_in_flash_func(process_sample)(uint16_t sample_in) {

    static int32_t global_dc_filter = 2048 << 14; 
    global_dc_filter += (sample_in - (global_dc_filter >> 14));
    int32_t true_dc = global_dc_filter >> 14;
    
    int32_t ac_sample = (int32_t)sample_in - true_dc;

    
    if (current_effect == FX_CLEAN) {
        return sample_in;
    }
    
    // DISTORTION
    if (current_effect == FX_DISTORTION) {
        static int32_t lp_in = 0;
        lp_in = (lp_in * 3 + ac_sample) >> 2; 
        
        int32_t distorted = lp_in * dist_gain;
        
        if (distorted > dist_clip) distorted = dist_clip;
        if (distorted < -dist_clip) distorted = -dist_clip;
        
        static int32_t lp_out = 0;
        lp_out = (lp_out * 7 + distorted) >> 3; 

        int32_t out_sample = true_dc + lp_out; // Use true_dc instead of 2048
        
        if (out_sample > 4095) out_sample = 4095;
        if (out_sample < 0) out_sample = 0;
        
        return (uint16_t)out_sample;
    }
    
    // DELAY 
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
    
    if (current_effect == FX_TREMOLO) { 
        
        // PHASE ACCUMULATOR
        static uint32_t lfo_phase = 0;
        
        uint32_t freq_hz = 1 + (trem_rate * 9) / 100; 
        uint32_t phase_inc = freq_hz * 85899; 
        
        lfo_phase += phase_inc; 
        
        // GENERATE HD TRIANGLE WAVE (12-bit: 0 to 4095)
        // Instead of grabbing the top 8 bits (>> 24), we grab the top 12 bits (>> 20)
        uint32_t top_12 = lfo_phase >> 20; 
        int32_t triangle = top_12;
        
        // Fold at the halfway point (2047 instead of 127)
        if (triangle > 2047) {
            triangle = 4095 - triangle; 
        }
        triangle = triangle << 1; // Scale up to roughly 0 - 4095

        // APPLY DEPTH (12-bit math)
        // 4095 represents 100% full volume.
        int32_t lfo_mult = 4095 - (((4095 - triangle) * trem_depth) / 100);

        // MODULATE AMPLITUDE
        // Multiply by our massive 12-bit LFO, then shift right by 12 (divide by 4096) 
        // to restore the audio scale.
        // Note: 4095 * 4095 = ~16.7 million, which easily fits inside our 32-bit int!
        int32_t tremolo_out = (ac_sample * lfo_mult) >> 12; 

        // RE-ADD DC BIAS & CLIP
        int32_t mixed = tremolo_out + true_dc;
        
        if (mixed > 4095) mixed = 4095;
        if (mixed < 0)    mixed = 0;

        return (uint16_t)mixed;
    }
    
    return sample_in;
}

// INTERRUPTS & PERIPHERALS


void __not_in_flash_func(dma_irq_handler)() {
    if (dma_channel_get_irq0_status(adc_dma_chan)) {
        dma_channel_acknowledge_irq0(adc_dma_chan);

        // IMMEDIATELY start DMA on the next buffer so we don't drop ADC samples!
        int next_write_buffer = dma_write_buffer ^ 1;
        uint16_t *next = (next_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        dma_channel_set_write_addr(adc_dma_chan, next, true);

        // Process the buffer that just finished filling
        uint16_t *src = (dma_write_buffer == 0) ? adc_buffer_0 : adc_buffer_1;
        uint16_t *dst = (dma_write_buffer == 0) ? out_buffer_0 : out_buffer_1;

        for (int i = 0; i < BUFFER_SIZE; i++) {
            dst[i] = process_sample(src[i]);
        }

        // Hand the processed buffer over to the PWM playback timer
        play_buffer = dma_write_buffer;
        
        // Update state for the next cycle
        dma_write_buffer = next_write_buffer;
    }
}

bool __not_in_flash_func(timer_callback)(struct repeating_timer *t) {
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

void update_leds() {
    gpio_put(LED_WHITE,      current_effect == FX_CLEAN);
    gpio_put(LED_RED, current_effect == FX_DISTORTION);
    gpio_put(LED_YELLOW,      current_effect == FX_DELAY);
    gpio_put(LED_BLUE,     current_effect == FX_TREMOLO);
}

void leds_init() {
    gpio_init(LED_WHITE);
    gpio_set_dir(LED_WHITE, GPIO_OUT);
    
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    
    gpio_init(LED_YELLOW);
    gpio_set_dir(LED_YELLOW, GPIO_OUT);
    
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    
    update_leds(); 
}


static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p != NULL) {
        char request[512];
        size_t len = p->len < 511 ? p->len : 511;
        memcpy(request, p->payload, len);
        request[len] = '\0';
        
        char response_header[256];
        const unsigned char* response_body = NULL;
        unsigned int response_body_len = 0;

        // EFFECT SWITCHING
        if (strstr(request, "GET /?fx=")) {
            if (strstr(request, "fx=clean"))  current_effect = FX_CLEAN;
            if (strstr(request, "fx=dist"))   current_effect = FX_DISTORTION;
            if (strstr(request, "fx=delay"))  current_effect = FX_DELAY;
            if (strstr(request, "fx=reverb")) current_effect = FX_TREMOLO;

            update_leds();
            
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n");
            response_body = (const unsigned char*)"FX Switched";
            response_body_len = 11;
        }
        // KNOBS
        else if (strstr(request, "GET /?param=")) {
            char param_name[32];
            int val = 0;
            
            // Extragem numele parametrului și valoarea
            if (sscanf(request, "GET /?param=%31[^&]&val=%d", param_name, &val) == 2) {
                
                // DISTORTION
                if (strcmp(param_name, "Distorsion") == 0) {
                    if (val == 0) val = 1;

                    dist_gain = (val * 50) / 100 + 1; 
                    dist_clip = 2000 - ((val * 1400) / 100); 
                    
                    printf("[WEB] Distortion updated: Gain=%d, Clip=%d\n", dist_gain, dist_clip);
                } 
                
                // DELAY (NOT WORKING)
                // else if (strcmp(param_name, "Delay") == 0) {
                //     if (val < 0) val = 0;
                //     if (val > 100) val = 100;

                //     if (val == 0) {
                //         delay_depth = 1; 
                //     } else {
                //         delay_depth = 1000 + ((val * (DELAY_MAX - 1000)) / 100); 
                //     }
                //     printf("[WEB] Delay Time updated: Depth=%d samples\n", delay_depth);
                // }
                
                // TREMOLO RATE
                else if (strcmp(param_name, "TremoloRate") == 0 || strcmp(param_name, "Reverb") == 0) {
                    if (val < 0) val = 0;
                    if (val > 100) val = 100;
            
                    trem_rate = val; 
                    printf("[WEB] Tremolo Rate updated: %d%%\n", trem_rate);
                }

                printf("Knob Update -> %s: %d\n", param_name, val);
            }
            
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n");
            response_body = (const unsigned char*)"Param Saved";
            response_body_len = 11;
        }
        // STATIC HTML
        else if (strstr(request, "GET / ") || strstr(request, "GET /index.html")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
            response_body = asset_index_html;
            response_body_len = asset_index_html_len;
        } 
        else if (strstr(request, "GET /control.html")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
            response_body = asset_control_html;
            response_body_len = asset_control_html_len;
        } 
        else if (strstr(request, "GET /delay.html")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
            response_body = asset_delay_html;
            response_body_len = asset_delay_html_len;
        } 
        else if (strstr(request, "GET /distors.html")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
            response_body = asset_distors_html;
            response_body_len = asset_distors_html_len;
        } 
        else if (strstr(request, "GET /reverb.html")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
            response_body = asset_reverb_html;
            response_body_len = asset_reverb_html_len;
        } 
        // CSS + JS
        else if (strstr(request, "GET /css/stil.css")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nConnection: close\r\n\r\n");
            response_body = asset_stil_css;
            response_body_len = asset_stil_css_len;
        } 
        else if (strstr(request, "GET /js/script.js")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nConnection: close\r\n\r\n");
            response_body = asset_script_js;
            response_body_len = asset_script_js_len;
        } 
        // AUDIO + IMAGES
        else if (strstr(request, "GET /audio/delay.ogg")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: audio/ogg\r\nConnection: close\r\n\r\n");
            response_body = asset_audio_delay;
            response_body_len = asset_audio_delay_len;
        }
        else if (strstr(request, "GET /audio/distors.ogg")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: audio/ogg\r\nConnection: close\r\n\r\n");
            response_body = asset_audio_distors;
            response_body_len = asset_audio_distors_len;
        }
        else if (strstr(request, "GET /audio/reverb.ogg")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: audio/ogg\r\nConnection: close\r\n\r\n");
            response_body = asset_audio_reverb;
            response_body_len = asset_audio_reverb_len;
        }
        else if (strstr(request, "GET /imagini/github.png")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n");
            response_body = asset_img_github;
            response_body_len = asset_img_github_len;
        }
        else if (strstr(request, "GET /imagini/raspberry.png")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n");
            response_body = asset_img_raspberry;
            response_body_len = asset_img_raspberry_len;
        }
        else if (strstr(request, "GET /imagini/favicon.ico")) {
            sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: image/x-icon\r\nConnection: close\r\n\r\n");
            response_body = asset_img_favicon;
            response_body_len = asset_img_favicon_len;
        }
        // SMTH ELSE => ERROR 404
        else {
            sprintf(response_header, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n");
            response_body = (const unsigned char*)"File not found";
            response_body_len = 14;
        }

        // WRITE PAYLOADS TO NETWORK
        tcp_write(tpcb, response_header, strlen(response_header), TCP_WRITE_FLAG_COPY);
        
        // 2. Write Binary/Text Content Body
        if (response_body && response_body_len > 0) {
            size_t snd_buf = tcp_sndbuf(tpcb);
            if (response_body_len <= snd_buf) {
                tcp_write(tpcb, response_body, response_body_len, 0);
            } else {
                tcp_write(tpcb, response_body, snd_buf, 0); 
                printf("Warning: Payload exceeded TCP buffer size!\n");
            }
        }
        
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        tcp_close(tpcb);
    }
    return ERR_OK;
}


static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback);
    return ERR_OK;
}


void core1_audio_loop() {
    //core 1
    my_adc_init();
    dma_init();
    my_pwm_init();
    timer_init();
    
    // Start ADC + DMA
    adc_run(true);
    dma_channel_start(adc_dma_chan);

    // Keep Core 1 alive forever
    while (true) {
        tight_loop_contents();
    }
}


int main() {
    stdio_init_all();
    leds_init();
    
    sleep_ms(2000);
    printf("Bass Pedal Starting...\n");

    // NEW WI-FI INITIALIZATION
    if (cyw43_arch_init()) {
        printf("Wi-Fi Init Failed!\n");
    } else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, 1);

        // Start Access Point Mode! 
        const char *ap_name = "PicoBassPedal";
        const char *password = "bass1234"; // 
        
        cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

        ip4_addr_t gw, mask;
        IP4_ADDR(&gw, 192, 168, 4, 1);
        IP4_ADDR(&mask, 255, 255, 255, 0);
        netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &gw, &mask, &gw);

        printf("Access Point Started! Connect to '%s' and go to 192.168.4.1\n", ap_name);

        struct tcp_pcb *pcb = tcp_new();
        tcp_bind(pcb, IP_ADDR_ANY, 80);
        pcb = tcp_listen(pcb);
        tcp_accept(pcb, connection_callback);
    }
    // --------------------------------


    multicore_launch_core1(core1_audio_loop);
    printf("Running at %d Hz sample rate\n", SAMPLE_RATE);


    while (true) {
        cyw43_arch_poll(); 
        sleep_ms(10);
    }

    return 0;
}