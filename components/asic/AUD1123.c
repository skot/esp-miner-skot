#include "AUD1123.h"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define AUD1123_CHIP_ID 0x1370
#define AUD1123_CHIP_ID_RESPONSE_LENGTH 11

#ifdef CONFIG_GPIO_ASIC_RESET
#define GPIO_ASIC_RESET CONFIG_GPIO_ASIC_RESET
#else
#define GPIO_ASIC_RESET 1
#endif

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_JOB 0x01

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define RESPONSE_CMD 0x00
#define RESPONSE_JOB 0x80

#define SLEEP_TIME 20
#define FREQ_MULT 25.0

#define CLOCK_ORDER_CONTROL_0 0x80
#define CLOCK_ORDER_CONTROL_1 0x84
#define ORDERED_CLOCK_ENABLE 0x20
#define CORE_REGISTER_CONTROL 0x3C
#define PLL3_PARAMETER 0x68
#define FAST_UART_CONFIGURATION 0x28
#define TICKET_MASK 0x14
#define MISC_CONTROL 0x18

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint16_t version;
    uint8_t crc;
} AUD1123_asic_result_t;

static const char * TAG = "AUD1123Module";

static task_result result;

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static void _send_AUD1123(uint8_t header, uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    // allocate memory for buffer
    unsigned char * buf = malloc(total_length);

    // add the preamble
    buf[0] = 0x55;
    buf[1] = 0xAA;

    // add the header field
    buf[2] = header;

    // add the length field
    buf[3] = (packet_type == JOB_PACKET) ? (data_len + 4) : (data_len + 3);

    // add the data
    memcpy(buf + 4, data, data_len);

    // add the correct crc type
    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    } else {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    // send serial data
    if (SERIAL_send(buf, total_length, debug) == 0) {
        ESP_LOGE(TAG, "Failed to send data to AUD1123");
    }

    free(buf);
}

static void _send_simple(uint8_t * data, uint8_t total_length)
{
    unsigned char * buf = malloc(total_length);
    memcpy(buf, data, total_length);
    SERIAL_send(buf, total_length, AUD1123_SERIALTX_DEBUG);

    free(buf);
}


void AUD1123_set_version_mask(uint32_t version_mask) 
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_AUD1123(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6, AUD1123_SERIALTX_DEBUG);
}

void AUD1123_send_hash_frequency(float target_freq) {
    // default 200Mhz if it fails
    unsigned char freqbuf[6] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41}; // freqbuf - pll0_parameter
    float newf = 200.0;

    uint8_t fb_divider = 0;
    uint8_t post_divider1 = 0, post_divider2 = 0;
    uint8_t ref_divider = 0;
    float min_difference = 10;
    float max_diff = 1.0;

    // refdiver is 2 or 1
    // postdivider 2 is 1 to 7
    // postdivider 1 is 1 to 7 and greater than or equal to postdivider 2
    // fbdiv is 0xa0 to 0xef
    for (uint8_t refdiv_loop = 2; refdiv_loop > 0 && fb_divider == 0; refdiv_loop--) {
        for (uint8_t postdiv1_loop = 7; postdiv1_loop > 0 && fb_divider == 0; postdiv1_loop--) {
            for (uint8_t postdiv2_loop = 7; postdiv2_loop > 0 && fb_divider == 0; postdiv2_loop--) {
                if (postdiv1_loop >= postdiv2_loop) {
                    int temp_fb_divider = round(((float) (postdiv1_loop * postdiv2_loop * target_freq * refdiv_loop) / 25.0));

                    if (temp_fb_divider >= 0xa0 && temp_fb_divider <= 0xef) {
                        float temp_freq = 25.0 * (float) temp_fb_divider / (float) (refdiv_loop * postdiv2_loop * postdiv1_loop);
                        float freq_diff = fabs(target_freq - temp_freq);

                        if (freq_diff < min_difference && freq_diff < max_diff) {
                            fb_divider = temp_fb_divider;
                            post_divider1 = postdiv1_loop;
                            post_divider2 = postdiv2_loop;
                            ref_divider = refdiv_loop;
                            min_difference = freq_diff;
                            newf = temp_freq;
                        }
                    }
                }
            }
        }
    }

    if (fb_divider == 0) {
        ESP_LOGE(TAG, "Failed to find PLL settings for target frequency %.2f", target_freq);
        return;
    }

    freqbuf[3] = fb_divider;
    freqbuf[4] = ref_divider;
    freqbuf[5] = (((post_divider1 - 1) & 0xf) << 4) + ((post_divider2 - 1) & 0xf);

    if (fb_divider * 25 / (float) ref_divider >= 2400) {
        freqbuf[2] = 0x50;
    }

    _send_AUD1123(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6, AUD1123_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %.2fMHz (%.2f)", target_freq, newf);
}

static void do_frequency_ramp_up(float target_frequency) {
    if (target_frequency == 0) {
        ESP_LOGI(TAG, "Skipping frequency ramp");
        return;
    }
    
    ESP_LOGI(TAG, "Ramping up frequency from 56.25 MHz to %.2f MHz", target_frequency);
    do_frequency_transition(target_frequency, AUD1123_send_hash_frequency, 1370);
}

// Add a public function for external use
bool AUD1123_set_frequency(float target_freq) {
    return do_frequency_transition(target_freq, AUD1123_send_hash_frequency, 1370);
}

static uint8_t _send_init(uint64_t frequency, uint16_t asic_count)
{

    return 0;
}

// reset the AUD1123 via the RTS line
static void _reset(void)
{
    gpio_set_level(GPIO_ASIC_RESET, 0);

    // delay for 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // set the gpio pin high
    gpio_set_level(GPIO_ASIC_RESET, 1);

    // delay for 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);
}


uint8_t AUD1123_init(uint64_t frequency, uint16_t asic_count)
{
    ESP_LOGI(TAG, "Initializing AUD1123");

    esp_rom_gpio_pad_select_gpio(GPIO_ASIC_RESET);
    gpio_set_direction(GPIO_ASIC_RESET, GPIO_MODE_OUTPUT);

    // reset the AUD1123
    _reset();

    return _send_init(frequency, asic_count);
}

// Baud formula = 25M/((denominator+1)*8)
// The denominator is 5 bits found in the misc_control (bits 9-13)
int AUD1123_set_default_baud(void)
{
    return 0;
}

int AUD1123_set_max_baud(void)
{
    return 0;
}


void AUD1123_set_job_difficulty_mask(int difficulty)
{
    return;
}

static uint8_t id = 0;

void AUD1123_send_work(void * pvParameters, bm_job * next_bm_job)
{

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    AUD1123_job job;
    id = (id + 24) % 128;
    job.job_id = id;
    job.num_midstates = 0x01;
    memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
    memcpy(&job.nbits, &next_bm_job->target, 4);
    memcpy(&job.ntime, &next_bm_job->ntime, 4);
    memcpy(job.merkle_root, next_bm_job->merkle_root_be, 32);
    memcpy(job.prev_block_hash, next_bm_job->prev_block_hash_be, 32);
    memcpy(&job.version, &next_bm_job->version, 4);

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[job.job_id] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    //debug sent jobs - this can get crazy if the interval is short
    #if AUD1123_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    //_send_AUD1123((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(AUD1123_job), AUD1123_DEBUG_WORK);
}

task_result * AUD1123_process_work(void * pvParameters)
{
    AUD1123_asic_result_t asic_result = {0};

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result)) == ESP_FAIL) {
        return NULL;
    }

    // uint8_t job_id = asic_result.job_id;
    // uint8_t rx_job_id = ((int8_t)job_id & 0xf0) >> 1;
    // ESP_LOGI(TAG, "Job ID: %02X, RX: %02X", job_id, rx_job_id);

    // uint8_t job_id = asic_result.job_id & 0xf8;
    // ESP_LOGI(TAG, "Job ID: %02X, Core: %01X", job_id, asic_result.job_id & 0x07);

    uint8_t job_id = (asic_result.job_id & 0xf0) >> 1;
    uint8_t core_id = (uint8_t)((ntohl(asic_result.nonce) >> 25) & 0x7f); // AUD1123 has 80 cores, so it should be coded on 7 bits
    uint8_t small_core_id = asic_result.job_id & 0x0f; // AUD1123 has 16 small cores, so it should be coded on 4 bits
    uint32_t version_bits = (ntohs(asic_result.version) << 13); // shift the 16 bit value left 13
    ESP_LOGI(TAG, "Job ID: %02X, Core: %d/%d, Ver: %08" PRIX32, job_id, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.nonce;
    result.rolled_version = rolled_version;

    return &result;
}
