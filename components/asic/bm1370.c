#include "bm_job_packet.h"
#include "bm1370.h"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "pll.h"

#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>

#define BM1370_CHIP_ID 0x1370
#define BM1370_CHIP_ID_RESPONSE_LENGTH 11

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define BM_CHIP_ID 0x00
#define MISC_CONTROL 0x18
#define FAST_UART_CONFIGURATION 0x28
#define CORE_REGISTER_CONTROL 0x3C
#define ANALOG_MUX_CONTROL 0x54
#define IO_DRIVER_STRENGTH 0x58
#define PLL3_PARAMETER 0x68
#define BM1370_CLOCK_DELAY_CTRL_VALUE 0x10

static const register_type_t REGISTER_MAP[256] = {
    [BM13XX_REG_ERROR_COUNT]    = REGISTER_ERROR_COUNT,
    [BM13XX_REG_DOMAIN_0_COUNT] = REGISTER_DOMAIN_0_COUNT,
    [BM13XX_REG_DOMAIN_1_COUNT] = REGISTER_DOMAIN_1_COUNT,
    [BM13XX_REG_DOMAIN_2_COUNT] = REGISTER_DOMAIN_2_COUNT,
    [BM13XX_REG_DOMAIN_3_COUNT] = REGISTER_DOMAIN_3_COUNT,
    [BM13XX_REG_TOTAL_COUNT]    = REGISTER_TOTAL_COUNT,
};

typedef struct __attribute__((__packed__))
{
    uint32_t nonce;                   // 2-5
    uint8_t midstate_num;             // 6
    uint8_t chip_slot;                // 7
    uint16_t version;                 // 8-9
} bm1370_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;                   // 2-5
    uint8_t asic_address;             // 6
    uint8_t register_address;         // 7
    uint16_t                  : 16;   // 8-9
} bm1370_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;                // 0-1
    union {
        bm1370_asic_result_job_t job; // 2-9
        bm1370_asic_result_cmd_t cmd; // 2-9
    };
    uint8_t crc             : 5;      // 10:0-5
    uint8_t                 : 2;      // 10:6-7
    uint8_t is_job_response : 1;      // 10:8
} bm1370_asic_result_t;

static const char * TAG = "bm1370";

static task_result result;

static uint16_t address_interval;
#define BM1370_MAX_CHIPS 16
#define BM1370_ADC_CTRL0 0xb8
#define BM1370_ADC_CTRL1 0xb9
#define BM1370_ADC_DATA 0xbd
#define BM1370_ADC_READY (1U << 31)
#define BM1370_SENSOR_MAX_AGE_US 10000000ULL
static uint8_t detected_chip_count;
static uint8_t sensor_step;
static pthread_mutex_t sensor_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    float temperature;
    float voltage_mv[BM1370_VOLTAGE_DOMAINS];
    uint64_t temperature_time;
    uint64_t voltage_time[BM1370_VOLTAGE_DOMAINS];
    uint32_t reply;
    uint64_t reply_time;
    bool pending;
    bool logged[6];
} sensors[BM1370_MAX_CHIPS];

float BM1370_get_die_temperature(uint8_t chip, uint64_t now_us)
{
    if (chip >= detected_chip_count) return NAN;
    pthread_mutex_lock(&sensor_lock);
    uint64_t time = sensors[chip].temperature_time;
    float temperature = sensors[chip].temperature;
    pthread_mutex_unlock(&sensor_lock);
    return time && now_us >= time && now_us - time <= BM1370_SENSOR_MAX_AGE_US ? temperature : NAN;
}

float BM1370_get_domain_voltage_mv(uint8_t chip, uint8_t domain, uint64_t now_us)
{
    if (chip >= detected_chip_count || domain >= BM1370_VOLTAGE_DOMAINS) return NAN;
    pthread_mutex_lock(&sensor_lock);
    uint64_t time = sensors[chip].voltage_time[domain];
    float mv = sensors[chip].voltage_mv[domain];
    pthread_mutex_unlock(&sensor_lock);
    return time && now_us >= time && now_us - time <= BM1370_SENSOR_MAX_AGE_US ? mv : NAN;
}

static bool _send_BM1370(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug);

static void BM1370_write_clock_delay_ctrl(uint8_t chip_addr, uint8_t header)
{
    _send_BM1370(header,
                 (uint8_t[]){chip_addr, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x80, BM1370_CLOCK_DELAY_CTRL_VALUE},
                 6,
                 BM1370_SERIALTX_DEBUG);
}

static void BM1370_write_io_driver_strength(uint8_t chip_addr, uint8_t header, uint8_t value2)
{
    _send_BM1370(header,
                 (uint8_t[]){chip_addr, IO_DRIVER_STRENGTH, 0x00, 0x01, value2, 0x11},
                 6,
                 BM1370_SERIALTX_DEBUG);
}

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static bool _send_BM1370(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    const uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    if (total_length > 128) {
        ESP_LOGE(TAG, "Packet length %d exceeds maximum buffer size", total_length);
        return false;
    }

    uint8_t buf[128];

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
    if (SERIAL_send(buf, total_length, debug) != total_length) {
        ESP_LOGE(TAG, "Failed to send data to BM1370");
        return false;
    }
    return true;
}

static void _send_chain_inactive(void)
{
    unsigned char read_address[] = {0x00, 0x00};
    // send serial data
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, BM1370_SERIALTX_DEBUG);
}

static void _set_chip_address(uint8_t chipAddr)
{
    unsigned char read_address[] = {chipAddr, 0x00};
    // send serial data
    _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, BM1370_SERIALTX_DEBUG);
}

void BM1370_set_version_mask(uint32_t version_mask) 
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_BM1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6, BM1370_SERIALTX_DEBUG);
}

void BM1370_set_hash_counting_number(uint32_t hcn) {
    uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
    set_10_hash_counting[2] = (hcn >> 24) & 0xFF;
    set_10_hash_counting[3] = (hcn >> 16) & 0xFF;
    set_10_hash_counting[4] = (hcn >> 8) & 0xFF;
    set_10_hash_counting[5] = hcn & 0xFF;
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6, BM1370_SERIALTX_DEBUG);
}

void BM1370_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores) 
{   
    int cores_up = _next_power_of_two(cores);
    int asic_count_up =  _next_power_of_two(asic_count);

    // HCN hash counting number (the size of the nonce space)
    float hcn_space = (float)NONCE_SPACE / cores_up / asic_count_up;
    double hcn_max = hcn_space * (double)FREQ_MULT / frequency * 0.5f; 
    // BM1370 has a HW errata of 134 per clock cycle
    // use 2x value overwise we can get duplicates
    int hcn_error = 2 * 134;
    double hcn_frac = nonce_percent * (hcn_max - hcn_error);
    uint32_t hcn_register_value = (uint32_t)hcn_frac;

    BM1370_set_hash_counting_number(hcn_register_value);
}

float BM1370_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &frequency);
    
    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    _send_BM1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6, BM1370_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, frequency);

    return frequency;
}

uint8_t BM1370_init(GlobalState * GLOBAL_STATE)
{
    pthread_mutex_lock(&sensor_lock);
    detected_chip_count = 0;
    sensor_step = 0;
    memset(sensors, 0, sizeof(sensors));
    pthread_mutex_unlock(&sensor_lock);
    // set version mask
    for (int i = 0; i < 3; i++) {
        BM1370_set_version_mask(BIP320_VERSION_ROLLING_MASK);
    }

    //read register 00 on all chips (should respond AA 55 13 68 00 00 00 00 00 00 0F)
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, BM_CHIP_ID}, 2, BM1370_SERIALTX_DEBUG);

    uint16_t asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int chip_counter = count_asic_chips(asic_count, BM1370_CHIP_ID, BM1370_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter <= 0 || chip_counter > BM1370_MAX_CHIPS) {
        return 0;
    }

    detected_chip_count = chip_counter;

    // set version mask
    BM1370_set_version_mask(BIP320_VERSION_ROLLING_MASK);

    //Reg_A8
    //unsigned char init5[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xA8, 0x00, 0x07, 0x00, 0x00}, 6, BM1370_SERIALTX_DEBUG);

    //Misc Control
    //TX: 55 AA 51 09 [00 18 F0 00 C1 00] 04 //command all chips, write chip address 00, register 18, data F0 00 C1 00 - Misc Control
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00}, 6, BM1370_SERIALTX_DEBUG); //from S21Pro dump
    //_send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00}, 6, BM1370_SERIALTX_DEBUG); //from S21 dump

    //chain inactive
    _send_chain_inactive();
    // unsigned char init7[7] = {0x55, 0xAA, 0x53, 0x05, 0x00, 0x00, 0x03};
    // _send_simple(init7, 7);

    // split the chip address space evenly
    address_interval = 256 / chip_counter;
    for (uint8_t i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
        // unsigned char init8[7] = {0x55, 0xAA, 0x40, 0x05, 0x00, 0x00, 0x1C};
        // _send_simple(init8, 7);
    }

    //Core Register Control
    //unsigned char init9[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00, 0x12};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x8B, 0x00}, 6, BM1370_SERIALTX_DEBUG);
    asic_init_core_register_delay();

    // Core Register Control, internal ClockDelayCtrlV2.
    BM1370_write_clock_delay_ctrl(0x00, (TYPE_CMD | GROUP_ALL | CMD_WRITE));
    asic_init_core_register_delay();

    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x14, 0x00, 0x00, 0x00, 0xFF}, 6, BM1370_SERIALTX_DEBUG);
    asic_init_core_register_delay();

    //Analog Mux Control
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, ANALOG_MUX_CONTROL, 0x00, 0x00, 0x00, 0x03}, 6, BM1370_SERIALTX_DEBUG);

    //Set the IO Driver Strength
    BM1370_write_io_driver_strength(0x00, (TYPE_CMD | GROUP_ALL | CMD_WRITE), 0x11);

    // Present in the older BM1370 C++ init path. Keep this before per-chip core
    // register setup so all chips share the same internal counter/mux state.
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, PLL3_PARAMETER, 0x5A, 0xA5, 0x5A, 0xA5}, 6, BM1370_SERIALTX_DEBUG);

    for (uint8_t i = 0; i < chip_counter; i++) {
        uint8_t chip_addr = i * address_interval;
        //TX: 55 AA 41 09 00 [A8 00 07 01 F0] 15    // Reg_A8
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), (uint8_t[]){chip_addr, 0xA8, 0x00, 0x07, 0x01, 0xF0}, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [18 F0 00 C1 00] 0C    // Misc Control
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), (uint8_t[]){chip_addr, MISC_CONTROL, 0xF0, 0x00, 0xC1, 0x00}, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [3C 80 00 8B 00] 1A    // Core Register Control
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), (uint8_t[]){chip_addr, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x8B, 0x00}, 6, BM1370_SERIALTX_DEBUG);
        asic_init_core_register_delay();
        //TX: 55 AA 41 09 00 [3C 80 00 80 XX] 09    // Core Register Control
        BM1370_write_clock_delay_ctrl(chip_addr, (TYPE_CMD | GROUP_SINGLE | CMD_WRITE));
        asic_init_core_register_delay();
        //TX: 55 AA 41 09 00 [3C 80 00 82 AA] 05    // Core Register Control
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), (uint8_t[]){chip_addr, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x82, 0xAA}, 6, BM1370_SERIALTX_DEBUG);
        asic_init_core_register_delay();
    }

    //Some misc settings?
    // TX: 55 AA 51 09 [00 B9 00 00 44 80] 0D    //command all chips, write chip address 00, register B9, data 00 00 44 80
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 54 00 00 00 02] 18    //command all chips, write chip address 00, register 54, data 00 00 00 02 - Analog Mux Control - rumored to control the temp diode
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, ANALOG_MUX_CONTROL, 0x00, 0x00, 0x00, 0x02}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 B9 00 00 44 80] 0D    //command all chips, write chip address 00, register B9, data 00 00 44 80 -- duplicate of first command in series
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 3C 80 00 8D EE] 1B    //command all chips, write chip address 00, register 3C, data 80 00 8D EE
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x8D, 0xEE}, 6, BM1370_SERIALTX_DEBUG);
    asic_init_core_register_delay();

    //ramp up the hash frequency
    do_frequency_transition(GLOBAL_STATE, BM1370_send_hash_frequency);

    float frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    int cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count;

    BM1370_set_nonce_space(1.0, frequency, asic_count, cores);

    return chip_counter;
}

// static void _send_read_address(void)
// {

//     unsigned char read_address[2] = {0x00, 0x00};
//     // send serial data
//     _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_READ), read_address, 2, BM1370_SERIALTX_DEBUG);
// }

int BM1370_set_max_baud(void)
{
    // divider of 0 for 3,125,000
    ESP_LOGI(TAG, "Setting max baud of 1000000 ⁠​‌‌​​​‌​​‌‌​‌​​‌​‌‌‌​‌​​​‌‌​​​​‌​‌‌‌‌​​​​‌‌​​‌​‌⁠");

    unsigned char fast_uart[] = {0x00, FAST_UART_CONFIGURATION, 0x11, 0x30, 0x02, 0x00};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), fast_uart, 6, BM1370_SERIALTX_DEBUG);
    return 1000000;
}

void BM1370_send_work(GlobalState * GLOBAL_STATE, const asic_job_t * next_job)
{
    bm13xx_job_packet_t job;
    bm13xx_build_job_packet(&GLOBAL_STATE->job_store, next_job, &job);
    _send_BM1370((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(job), BM1370_DEBUG_WORK);
}

task_result * BM1370_process_work(GlobalState * GLOBAL_STATE)
{
    bm1370_asic_result_t asic_result = {0};

    memset(&result, 0, sizeof(task_result));

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result), &result.timestamp_us) == ESP_FAIL) {
        return NULL;
    }
    
    if (!asic_result.is_job_response) {
        if (!address_interval || asic_result.cmd.asic_address % address_interval ||
            asic_result.cmd.asic_address / address_interval >= detected_chip_count) return NULL;
        uint8_t chip = asic_result.cmd.asic_address / address_interval;
        if (asic_result.cmd.register_address == BM1370_ADC_DATA) {
            uint32_t value = ntohl(asic_result.cmd.value);
            pthread_mutex_lock(&sensor_lock);
            if (sensors[chip].pending) {
                sensors[chip].reply = value;
                sensors[chip].reply_time = result.timestamp_us;
                sensors[chip].pending = false;
            }
            pthread_mutex_unlock(&sensor_lock);
            return NULL;
        }
        result.register_type = REGISTER_MAP[asic_result.cmd.register_address];
        if (result.register_type == REGISTER_INVALID) {
            ESP_LOGW(TAG, "Unknown register read: %02x", asic_result.cmd.register_address);
            return NULL;
        }
        result.asic_nr = asic_result.cmd.asic_address / address_interval;
        result.value = ntohl(asic_result.cmd.value);
        
        return &result;
    }

    if (!bm13xx_decode_job_result(&GLOBAL_STATE->job_store,
                                  BM1370_SMALL_CORE_BITS,
                                  address_interval,
                                  asic_result.job.nonce,
                                  asic_result.job.chip_slot,
                                  asic_result.job.version,
                                  &result)) {
        return NULL;
    }

    return &result;
}

void BM1370_read_registers(GlobalState * GLOBAL_STATE)
{
    uint16_t asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    if (asic_count == 0 || address_interval == 0) {
        return;
    }

    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (uint8_t chip = 0; chip < asic_count; chip++) {
        uint8_t chip_addr = chip * address_interval;
        for (int reg = 0; reg < size; reg++) {
            if (REGISTER_MAP[reg] != REGISTER_INVALID) {
                _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_READ), (uint8_t[]){chip_addr, reg}, 2, BM1370_SERIALTX_DEBUG);
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }
}

static bool write_sensor_register(uint8_t reg, uint32_t value)
{
    return _send_BM1370(TYPE_CMD | GROUP_ALL | CMD_WRITE,
                       (uint8_t[]){0, reg, value >> 24, value >> 16, value >> 8, value},
                       6, BM1370_SERIALTX_DEBUG);
}

static bool configure_sensor(const uint32_t (*registers)[2], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!write_sensor_register(registers[i][0], registers[i][1])) return false;
    }
    return true;
}

static void read_sensor_samples(GlobalState *state, bool *valid, uint32_t *sums,
                                uint64_t *times, bool temperature, uint8_t phase_or_domain)
{
    unsigned samples = temperature ? 21 : 10;
    static const uint8_t selectors[] = {1, 2, 4, 5};
    uint32_t start = temperature ? (phase_or_domain ? 0x23ace00d : 0x23ac000d) :
                                 0x232c030d | ((uint32_t)selectors[phase_or_domain] << 13);
    for (unsigned sample = 0; sample < samples; sample++) {
        if (!state->ASIC_initalized || !write_sensor_register(BM1370_ADC_CTRL0, start) ||
            (temperature && !write_sensor_register(BM1370_ADC_CTRL1, 0x3f4141af))) {
            for (uint8_t chip = 0; chip < detected_chip_count; chip++) valid[chip] = false;
            break;
        }
        if (!temperature) vTaskDelay(pdMS_TO_TICKS(60));
        if (!state->ASIC_initalized) {
            for (uint8_t chip = 0; chip < detected_chip_count; chip++) valid[chip] = false;
            break;
        }
        bool any_valid = false;
        for (uint8_t chip = 0; chip < detected_chip_count; chip++) {
            if (!valid[chip]) continue;
            any_valid = true;
            pthread_mutex_lock(&sensor_lock);
            sensors[chip].pending = true;
            sensors[chip].reply = 0;
            pthread_mutex_unlock(&sensor_lock);
            if (!_send_BM1370(TYPE_CMD | GROUP_SINGLE | CMD_READ,
                             (uint8_t[]){chip * address_interval, BM1370_ADC_DATA},
                             2, BM1370_SERIALTX_DEBUG)) valid[chip] = false;
        }
        if (!any_valid) break;
        for (unsigned wait = 0; wait < 20 && state->ASIC_initalized; wait++) {
            bool pending = false;
            pthread_mutex_lock(&sensor_lock);
            for (uint8_t chip = 0; chip < detected_chip_count; chip++) {
                pending |= valid[chip] && sensors[chip].pending;
            }
            pthread_mutex_unlock(&sensor_lock);
            if (!pending) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        pthread_mutex_lock(&sensor_lock);
        for (uint8_t chip = 0; chip < detected_chip_count; chip++) {
            valid[chip] = valid[chip] && !sensors[chip].pending &&
                          (sample == 0 || (sensors[chip].reply & BM1370_ADC_READY));
            sensors[chip].pending = false;
            if (valid[chip] && sample > 0) {
                uint32_t raw = sensors[chip].reply;
                sums[chip] += (temperature && phase_or_domain ? raw >> 16 : raw) & 0x7fff;
                times[chip] = sensors[chip].reply_time;
                uint8_t log_index = temperature ? phase_or_domain : phase_or_domain + 2;
                if (!sensors[chip].logged[log_index]) {
                    sensors[chip].logged[log_index] = true;
                    ESP_LOGI(TAG, "ASIC %u %s %u raw ADC: 0x%08" PRIX32, chip,
                             temperature ? "temperature phase" : "voltage channel", phase_or_domain + 1, raw);
                }
            }
        }
        pthread_mutex_unlock(&sensor_lock);
    }
}

void BM1370_read_sensors(GlobalState *state)
{
    if (!state->ASIC_initalized || !detected_chip_count) return;
    bool temperature = sensor_step == 0;
    uint8_t domain = temperature ? 0 : sensor_step - 1;
    bool valid[BM1370_MAX_CHIPS];
    uint32_t sums[2][BM1370_MAX_CHIPS] = {0};
    uint64_t times[BM1370_MAX_CHIPS] = {0};
    for (uint8_t chip = 0; chip < detected_chip_count; chip++) valid[chip] = true;
    if (temperature) {
        static const uint32_t phase_a[][2] = {{0xb8, 0x238c000d}, {0xba, 0x00040010}, {0xbb, 0x03340e80}};
        static const uint32_t phase_b[][2] = {
            {0xb8, 0x238ce00d}, {0xb9, 0x3f0141af}, {0xba, 0x00040010}, {0xbb, 0x03340e80}};
        for (uint8_t phase = 0; phase < 2; phase++) {
            bool configured = state->ASIC_initalized && (phase ? configure_sensor(phase_b, 4) : configure_sensor(phase_a, 3));
            if (!configured) {
                for (uint8_t chip = 0; chip < detected_chip_count; chip++) valid[chip] = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            read_sensor_samples(state, valid, sums[phase], times, true, phase);
        }
    } else {
        static const uint8_t selectors[] = {1, 2, 4, 5};
        const uint32_t config[][2] = {
            {0xb8, 0x230c030d | ((uint32_t)selectors[domain] << 13)},
            {0xb9, 0x3f014381}, {0xba, 0x00040010}, {0xbb, 0x03340e80}};
        if (configure_sensor(config, 4)) {
            read_sensor_samples(state, valid, sums[0], times, false, domain);
        } else {
            for (uint8_t chip = 0; chip < detected_chip_count; chip++) valid[chip] = false;
        }
    }
    pthread_mutex_lock(&sensor_lock);
    for (uint8_t chip = 0; chip < detected_chip_count; chip++) {
        sensors[chip].pending = false;
        bool available = valid[chip] && state->ASIC_initalized;
        if (temperature) {
            uint32_t a = sums[0][chip] / 20, b = sums[1][chip] / 20;
            uint32_t code = a + 16384 - ((a + b) >> 1);
            sensors[chip].temperature = (float)(code * 0.0309 - 769.25);
            sensors[chip].temperature_time = available ? times[chip] : 0;
        } else {
            double mean = sums[0][chip] / 9.0;
            sensors[chip].voltage_mv[domain] = (float)((domain < 2 ? 1000.0 : 2000.0) * (mean / 16384.0 - 1.0));
            sensors[chip].voltage_time[domain] = available ? times[chip] : 0;
        }
    }
    // The ADC is shared: acquire a full temperature measurement or one voltage channel per poll.
    if (state->ASIC_initalized) sensor_step = (sensor_step + 1) % (BM1370_VOLTAGE_DOMAINS + 1);
    pthread_mutex_unlock(&sensor_lock);
}

void BM1370_set_difficulty(uint8_t difficulty_power)
{
    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty_power, difficulty_mask);
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask, 6, BM1370_SERIALTX_DEBUG);
}
