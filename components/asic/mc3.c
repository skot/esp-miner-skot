#include "mc3.h"

#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "global_state.h"
#include "hashrate_monitor_task.h"
#include "serial.h"

#define MC3_START_FLAG 0x55
#define MC3_CHIP_NUM_ALL 0x8
#define MC3_MODE_WRITE 0x1
#define MC3_MODE_READ 0x2
#define MC3_MODE_INIT 0x4

#define MC3_ULINK_FIELD_BYTES 5
#define MC3_ULINK_READ_META_FIELDS 3
#define MC3_ULINK_RESPONSE_DELAY_MS 2
#define MC3_INIT_RESPONSE_BYTES 15

#define MC3_THERMAL_SDIF_ENABLE 0x00051020
#define MC3_THERMAL_CLOCK 0x00051010
#define MC3_THERMAL_COMMAND 0x00051028
#define MC3_THERMAL_STATUS 0x00051024
#define MC3_THERMAL_DONE 0x00051238
#define MC3_THERMAL_DOUT 0x00051240
#define MC3_VOLTAGE_SDIF_ENABLE 0x00053020
#define MC3_VOLTAGE_CLOCK 0x00053010
#define MC3_VOLTAGE_COMMAND 0x00053028
#define MC3_VOLTAGE_STATUS 0x00053024
#define MC3_VOLTAGE_DONE 0x00053238
#define MC3_VOLTAGE_DOUT_BASE 0x00053240
#define MC3_VOLTAGE_CLOCK_DEFAULT 0x01010000

#define MC3_GLOBAL_SPD 0x00011000
#define MC3_SPDLOG_TIMER 0x00011004
#define MC3_SPDLOG_RST 0x00011008
#define MC3_SPDLOG_PASS 0x0001100C
#define MC3_SPDLOG_FAIL 0x00011010
#define MC3_PLL0_CFG 0x00040000
#define MC3_PLL0_EN 0x00040004
#define MC3_WORK_CFG 0x00000400
#define MC3_WORK_TARGET_L 0x00000404
#define MC3_WORK_TARGET_H 0x00000408
#define MC3_ROLLTIME 0x00000410
#define MC3_NONCE_UPDATE 0x00000424
#define MC3_V_BASE 0x00000440
#define MC3_V_CTRL 0x00000444

#define MC3_NONCE_BUFFER 0x00000300
#define MC3_NONCE_BUFFER_NOTE_WORDS 4
#define MC3_VERSION_ROLLING_NONCE_COUNT 5
#define MC3_NONCE_BUFFER_WORDS (MC3_NONCE_BUFFER_NOTE_WORDS + MC3_VERSION_ROLLING_NONCE_COUNT * 3)
#define MC3_NONCE_VALID_BIT 0x80000000
#define MC3_NONCE_UPDATE_BIT 0x00000001
#define MC3_NONCE_READY_BIT 0x00000002
#define MC3_NONCE_POLL_INTERVAL_US 500000
#define MC3_PENDING_RESULTS_SIZE 20
#define MC3_MAX_TRACKED_CHIPS 256
#define MC3_SPDLOG_TIMER_ENABLE 0x00010000
#define MC3_SPDLOG_XCLK_HZ 12500000.0
#define MC3_SPDLOG_TIMER_COUNT 10
#define MC3_SPDLOG_CORE_TARGET 0x00
#define MC3_SPDLOG_POLL_INTERVAL_US 3000000
#define MC3_CORE_SPDLOG_BANK0 0x00012000
#define MC3_CORE_SPDLOG_BANK1 0x00012400
#define MC3_CORE_SPDLOG_BANK_CORE_COUNT 78
#define MC3_CORE_SPDLOG_REGISTER_STRIDE 4
#define MC3_CORE_SPDLOG_PASS_MASK 0x0000FFFF
#define MC3_CORE_PASS_READ_CHUNK 24
#define MC3_TUNING_SPDLOG_TIMER_COUNT 48
#define MC3_TUNING_SETTLE_TIME_US 250000
#define MC3_THERMAL_RESOLUTION_BITS 12
#define MC3_VOLTAGE_RESOLUTION_BITS 14
#define MC3_VOLTAGE_VREF 0.5945f
#define MC3_VOLTAGE_VDD_CHANNEL_A 13
#define MC3_VOLTAGE_VDD_CHANNEL_B 14
#define MC3_VOLTAGE_VDD_SCALE 2.0f

#define MC3_V_WORK_INIT 0x00000200
#define MC3_V_WORK_NTIME 0x00000204
#define MC3_V_WORK_NBITS 0x00000208
#define MC3_V_WORK_MERKLE_ROOT 0x0000020C
#define MC3_V_WORK_PREVIOUS_HASH 0x0000022C
#define MC3_V_WORK_NOTE 0x0000024C

#define MC3_DEFAULT_GLOBAL_SPD_VALUE 0x80000570
#define MC3_DEFAULT_WORK_CFG_APPLY_AND_RESET 0x8800D2FF
#define MC3_DEFAULT_WORK_CFG_MINING 0x880058FF
#define MC3_DEFAULT_WORK_CFG_SPDLOG 0x8C00DA00
#define MC3_DEFAULT_V_CTRL_VERSION_ROLLING 0xFFFF0001
#define MC3_DEFAULT_WORK_TARGET_L 0xFFFFFFFF
#define MC3_DEFAULT_WORK_TARGET_H 0xFFFFFFFF
#define MC3_PDIFF1_WORK_TARGET 0xFFFF000000000000ULL
#define MC3_DEFAULT_VERSION_MASK_BITS 16
#define MC3_V_CTRL_START_BIT_SHIFT 4
#define MC3_V_CTRL_START_BIT_MASK (0x1FU << MC3_V_CTRL_START_BIT_SHIFT)
#define MC3_PLL0_ENABLE 0x00000001
#define MC3_DEFAULT_PLL0_ENABLE 0x00000003
#define MC3_PLL_LOCK_BIT 0x00000004
#define MC3_ROLLTIME_OFFSET 50
#define MC3_FREQUENCY_RAMP_DELAY_MS 500
#define MC3_FREQUENCY_HIGH_RAMP_DELAY_MS 1000
#define MC3_FREQUENCY_HIGH_THRESHOLD_MHZ 500
#define MC3_PLL_LOCK_TIMEOUT_MS 250
#define MC3_PLL_LOCK_POLL_MS 10
#define MC3_QUALIFICATION_SPDLOG_TIMER_COUNT 48
#define MC3_QUALIFICATION_SETTLE_TIME_MS 250
#define MC3_QUALIFICATION_VERSION_MASK 0xFFFF0000
#define MC3_QUALIFICATION_VERSION 0x20000000
#define MC3_QUALIFICATION_NTIME 0x65000000
#define MC3_QUALIFICATION_NBITS 0x1D00FFFF
#define MC3_QUALIFICATION_POOL_DIFFICULTY 1000000.0

_Static_assert(MC3_CORE_SPDLOG_BANK_CORE_COUNT * 2 == ASIC_TUNING_MAX_CORES,
    "MC3 per-core SPDLOG banks must cover every tuning core");

static const char *TAG = "mc3";

static task_result result;
static uint8_t mc3_chip_count;
static uint8_t mc3_job_id;
static int64_t mc3_last_nonce_poll_us;
static uint8_t mc3_next_nonce_chip;
static bool mc3_spdlog_started;
static int64_t mc3_spdlog_start_us;
static mc3_frequency_step_callback_t mc3_frequency_step_callback;
static task_result mc3_pending_results[MC3_PENDING_RESULTS_SIZE];
static uint8_t mc3_pending_head;
static uint8_t mc3_pending_count;
static uint32_t mc3_seen_nonce_words[MC3_MAX_TRACKED_CHIPS][MC3_VERSION_ROLLING_NONCE_COUNT][3];
static asic_tuning_status_t mc3_core_scan_status = {
    .supported = true,
    .state = ASIC_TUNING_IDLE,
    .core_count = ASIC_TUNING_MAX_CORES,
    .leading_zeros = 24,
};
static asic_tuning_chip_result_t mc3_core_scan_results[ASIC_TUNING_MAX_CHIPS];
static int64_t mc3_core_scan_start_us;
static pthread_mutex_t mc3_serial_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mc3_pending_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mc3_core_scan_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mc3_work_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    uint16_t frequency_mhz;
    uint8_t refdiv;
    uint16_t fbdiv;
    uint8_t postdiv1;
    uint8_t postdiv2;
} mc3_pll_config_t;

static const mc3_pll_config_t PLL_CONFIGS[] = {
    {100, 1, 160, 7, 4},
    {125, 1, 160, 7, 3},
    {150, 1, 150, 4, 4},
    {175, 1, 147, 6, 2},
    {200, 1, 160, 4, 3},
    {225, 1, 144, 7, 1},
    {250, 1, 160, 7, 1},
    {275, 1, 154, 6, 1},
    {300, 1, 144, 5, 1},
    {325, 1, 156, 5, 1},
    {350, 1, 140, 4, 1},
    {375, 1, 150, 4, 1},
    {400, 1, 160, 4, 1},
    {425, 1, 153, 2, 2},
    {450, 1, 144, 7, 0},
    {475, 1, 152, 7, 0},
    {500, 1, 160, 7, 0},
    {525, 1, 147, 6, 0},
    {550, 1, 154, 6, 0},
    {575, 1, 138, 5, 0},
    {600, 1, 144, 5, 0},
    {625, 1, 150, 5, 0},
    {650, 1, 156, 5, 0},
    {675, 1, 135, 4, 0},
    {700, 1, 140, 4, 0},
    {725, 1, 145, 4, 0},
    {750, 1, 150, 4, 0},
    {775, 1, 155, 4, 0},
    {800, 1, 160, 4, 0},
    {825, 1, 132, 3, 0},
    {850, 1, 136, 3, 0},
    {875, 1, 140, 3, 0},
    {900, 1, 144, 3, 0},
    {925, 1, 148, 3, 0},
    {950, 1, 152, 3, 0},
    {975, 1, 156, 3, 0},
    {1000, 1, 160, 3, 0},
};

static const char *ECC_TB_0 =
    "0c465e1458120a405b1109430f455d17541e064c004a52180349511b571d054f"
    "571d054f0349511b004a5218541e064c0f455d175b11094358120a400c465e14"
    "511b0349054f571d064c541e5218004a09435b115d170f455e140c460a405812"
    "0a4058125e140c465d170f4509435b115218004a064c541e054f571d511b0349"
    "2f657d377b31296378322a602c667e34773d256f2369713b206a7238743e266c"
    "743e266c206a72382369713b773d256f2c667e3478322a607b3129632f657d37"
    "7238206a266c743e256f773d713b23692a6078327e342c667d372f6529637b31"
    "29637b317d372f657e342c662a607832713b2369256f773d266c743e7238206a";

static const char *ECC_TB_1 =
    "0c292a0f25000326260300250f2a290c20050623092c2f0a0a2f2c0923060520"
    "3d181b3e14313217173231143e1b183d11343712381d1e3b3b1e1d3812373411"
    "381d1e3b11343712123734113b1e1d38143132173d181b3e3e1b183d17323114"
    "092c2f0a20050623230605200a2f2c09250003260c292a0f0f2a290c26030025"
    "022724012b0e0d28280d0e2b012427022e0b082d07222104042122072d080b2e"
    "331615301a3f3c19193c3f1a301516331f3a391c36131035351013361c393a1f"
    "361310351f3a391c1c393a1f351013361a3f3c193316153030151633193c3f1a"
    "072221042e0b082d2d080b2e042122072b0e0d280227240101242702280d0e2b";

static const char *ECC_TB_2 =
    "0c071f1419120a011a1109020f041c17151e060d000b13180308101b161d050e"
    "161d050e0308101b000b1318151e060d0f041c171a11090219120a010c071f14"
    "101b0308050e161d060d151e1318000b09021a111c170f041f140c070a011912"
    "0a0119121f140c071c170f0409021a111318000b060d151e050e161d101b0308"
    "6e657d767b70686378736b606d667e75777c646f6269717a616a7279747f676c"
    "747f676c616a72796269717a777c646f6d667e7578736b607b7068636e657d76"
    "7279616a676c747f646f777c717a62696b6078737e756d667d766e6568637b70"
    "68637b707d766e657e756d666b607873717a6269646f777c676c747f7279616a";

static const char *ECC_TB_3 =
    "0c686b0f64000367670300640f6b680c61050662096d6e0a0a6e6d0962060561"
    "7c181b7f14707317177370147f1b187c11757612791d1e7a7a1e1d7912767511"
    "791d1e7a11757612127675117a1e1d79147073177c181b7f7f1b187c17737014"
    "096d6e0a61050662620605610a6e6d09640003670c686b0f0f6b680c67030064"
    "523635513a5e5d39395d5e3a513536523f5b583c57333054543033573c585b3f"
    "224645214a2e2d49492d2e4a214546224f2b284c27434024244043274c282b4f"
    "274340244f2b284c4c282b4f244043274a2e2d492246452121454622492d2e4a"
    "573330543f5b583c3c585b3f543033573a5e5d395236355151353652395d5e3a";

typedef struct {
    uint32_t word;
    bool tag;
    uint8_t ecc;
    bool ecc_ok;
} mc3_field_t;

static bool mc3_write_register(uint8_t chip_id, uint32_t reg, uint32_t value, uint8_t chip_num);
static bool mc3_read_register(uint8_t chip_id, uint32_t reg, uint32_t *value);
static bool mc3_read_register_block(uint8_t chip_id, uint32_t reg, uint32_t *values, uint8_t value_count);
static bool mc3_pending_push(const task_result *new_result);
static asic_tuning_state_t mc3_get_core_scan_state(void);

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

static uint8_t ecc_table_lookup(const char *table, uint8_t index)
{
    return (hex_nibble(table[index * 2]) << 4) | hex_nibble(table[index * 2 + 1]);
}

static uint8_t mc3_calculate_ecc(uint32_t word, bool is_tail)
{
    uint8_t ecc = ecc_table_lookup(ECC_TB_0, word & 0xFF)
        ^ ecc_table_lookup(ECC_TB_1, (word >> 8) & 0xFF)
        ^ ecc_table_lookup(ECC_TB_2, (word >> 16) & 0xFF)
        ^ ecc_table_lookup(ECC_TB_3, (word >> 24) & 0xFF);

    ecc ^= is_tail ? 0xC3 : 0x0C;
    return ecc & 0x7F;
}

static void write_u32_be(uint8_t *buffer, uint32_t word)
{
    buffer[0] = (word >> 24) & 0xFF;
    buffer[1] = (word >> 16) & 0xFF;
    buffer[2] = (word >> 8) & 0xFF;
    buffer[3] = word & 0xFF;
}

static uint32_t read_u32_be(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0] << 24)
        | ((uint32_t)buffer[1] << 16)
        | ((uint32_t)buffer[2] << 8)
        | buffer[3];
}

static void mc3_encode_field(uint8_t *buffer, uint32_t word, bool is_tail)
{
    write_u32_be(buffer, word);
    buffer[4] = (is_tail ? 0x80 : 0x00) | mc3_calculate_ecc(word, is_tail);
}

static mc3_field_t mc3_decode_field(const uint8_t *buffer)
{
    mc3_field_t field = {0};
    field.word = read_u32_be(buffer);
    field.tag = (buffer[4] & 0x80) != 0;
    field.ecc = buffer[4] & 0x7F;
    field.ecc_ok = field.ecc == mc3_calculate_ecc(field.word, field.tag);
    return field;
}

static uint32_t mc3_build_header(uint8_t chip_id, uint8_t chip_num, uint8_t mode, uint8_t data_length)
{
    return ((uint32_t)MC3_START_FLAG << 24)
        | ((uint32_t)chip_id << 16)
        | (((uint32_t)chip_num & 0x0F) << 12)
        | (((uint32_t)mode & 0x0F) << 8)
        | data_length;
}

static const mc3_pll_config_t *mc3_get_pll_config(float frequency_mhz)
{
    uint16_t requested = (uint16_t)(frequency_mhz + 0.5f);
    const mc3_pll_config_t *best = &PLL_CONFIGS[0];

    for (int i = 0; i < sizeof(PLL_CONFIGS) / sizeof(PLL_CONFIGS[0]); i++) {
        if (PLL_CONFIGS[i].frequency_mhz == requested) {
            return &PLL_CONFIGS[i];
        }
        uint16_t best_delta = best->frequency_mhz > requested ? best->frequency_mhz - requested : requested - best->frequency_mhz;
        uint16_t candidate_delta = PLL_CONFIGS[i].frequency_mhz > requested
            ? PLL_CONFIGS[i].frequency_mhz - requested
            : requested - PLL_CONFIGS[i].frequency_mhz;
        if (candidate_delta < best_delta) {
            best = &PLL_CONFIGS[i];
        }
    }

    ESP_LOGW(TAG, "Unsupported MC3 frequency %.2f MHz, using nearest supported %u MHz", frequency_mhz, best->frequency_mhz);
    return best;
}

static uint32_t mc3_pll_config_value(const mc3_pll_config_t *config)
{
    return (1 << 3)
        | ((uint32_t)config->fbdiv << 4)
        | ((uint32_t)config->refdiv << 16)
        | ((uint32_t)config->postdiv1 << 22)
        | ((uint32_t)config->postdiv2 << 25);
}

static uint32_t mc3_rolltime_for_frequency(uint16_t frequency_mhz)
{
    return (uint32_t)(((16777216.0 * 12.5) / frequency_mhz) + 0.5) - MC3_ROLLTIME_OFFSET;
}

static float mc3_temperature_from_dout(uint32_t raw)
{
#if MC3_THERMAL_RESOLUTION_BITS >= 11
    float offset = 1.0f / (float)(1U << (MC3_THERMAL_RESOLUTION_BITS - 11));
#else
    float offset = (float)(1U << (11 - MC3_THERMAL_RESOLUTION_BITS));
#endif

    return 675.61f * (((float)raw - offset) / 4096.0f) - 288.82f;
}

static float mc3_voltage_from_dout(uint32_t raw)
{
    float scale = (7.0f * MC3_VOLTAGE_VREF) / 15.0f;
    return scale * (((6.0f * (float)raw) / (float)(1U << 14))
        - (3.0f / (float)(1U << MC3_VOLTAGE_RESOLUTION_BITS)) - 1.0f);
}

static uint32_t mc3_voltage_dout_register(uint8_t channel)
{
    return MC3_VOLTAGE_DOUT_BASE + ((uint32_t)channel * 4);
}

static uint32_t mc3_bswap32(uint32_t value)
{
    return ((value & 0x000000FF) << 24)
        | ((value & 0x0000FF00) << 8)
        | ((value & 0x00FF0000) >> 8)
        | ((value & 0xFF000000) >> 24);
}

static uint32_t mc3_current_rolltime(GlobalState *GLOBAL_STATE)
{
    float frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;
    if (frequency <= 0) {
        frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    }

    const mc3_pll_config_t *config = mc3_get_pll_config(frequency);
    return mc3_rolltime_for_frequency(config->frequency_mhz);
}

static uint32_t mc3_bm_field_word(const uint8_t field[32], uint8_t word_index)
{
    return read_u32_be(field + word_index * 4);
}

static bool mc3_version_start_bit(uint32_t version_mask, uint8_t *start_bit)
{
    const uint32_t rolling_bits = (1U << MC3_DEFAULT_VERSION_MASK_BITS) - 1;

    for (uint8_t bit = 0; bit <= 32 - MC3_DEFAULT_VERSION_MASK_BITS; bit++) {
        uint32_t rolling_mask = rolling_bits << bit;
        if ((version_mask & rolling_mask) == rolling_mask) {
            *start_bit = bit;
            return true;
        }
    }

    return false;
}

static uint32_t mc3_version_rolling_mask(uint8_t start_bit)
{
    return ((1U << MC3_DEFAULT_VERSION_MASK_BITS) - 1) << start_bit;
}

static uint32_t mc3_v_ctrl(uint8_t start_bit)
{
    return (MC3_DEFAULT_V_CTRL_VERSION_ROLLING & ~MC3_V_CTRL_START_BIT_MASK)
        | ((uint32_t)start_bit << MC3_V_CTRL_START_BIT_SHIFT);
}

static void mc3_store_active_job(GlobalState *GLOBAL_STATE, uint8_t job_id, bm_job *next_bm_job)
{
    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]);
    }
    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] = next_bm_job;
    GLOBAL_STATE->valid_jobs[job_id] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
}

static bool mc3_write_mining_config(uint32_t rolltime, uint32_t version_mask)
{
    uint8_t start_bit = 0;
    if (!mc3_version_start_bit(version_mask, &start_bit)) {
        ESP_LOGE(TAG, "MC3 requires %u contiguous version-mask bits; pool mask is 0x%08" PRIX32,
            MC3_DEFAULT_VERSION_MASK_BITS, version_mask);
        return false;
    }

    uint32_t v_ctrl = mc3_v_ctrl(start_bit);
    ESP_LOGI(TAG, "Writing MC3 mining config: rolltime=0x%08" PRIX32
        " version_mask=0x%08" PRIX32 " start_bit=%u V_CTRL=0x%08" PRIX32,
        rolltime, version_mask, start_bit, v_ctrl);

    mc3_write_register(0, MC3_GLOBAL_SPD, MC3_DEFAULT_GLOBAL_SPD_VALUE, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_ROLLTIME, rolltime, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_WORK_CFG, MC3_DEFAULT_WORK_CFG_APPLY_AND_RESET, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_WORK_CFG, MC3_DEFAULT_WORK_CFG_MINING, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_CTRL, v_ctrl, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_PLL0_EN, MC3_DEFAULT_PLL0_ENABLE, MC3_CHIP_NUM_ALL);
    return true;
}

static uint64_t mc3_work_target_from_difficulty(double difficulty)
{
    if (difficulty <= 1.0) {
        return UINT64_MAX;
    }

    double target = (double)UINT64_MAX / difficulty;
    if (target < 1.0) {
        return 1;
    }

    return (uint64_t)target;
}

static double mc3_work_target_effective_difficulty(uint64_t target)
{
    if (target == 0) {
        return 0.0;
    }

    return (double)MC3_PDIFF1_WORK_TARGET / (double)target;
}

static void mc3_write_work_target(double difficulty, uint8_t chip_count)
{
    uint64_t target = mc3_work_target_from_difficulty(difficulty);
    uint32_t target_h = (uint32_t)(target >> 32);
    uint32_t target_l = (uint32_t)(target & 0xFFFFFFFF);

    ESP_LOGI(TAG, "Writing MC3 work target: H=0x%08" PRIX32 " L=0x%08" PRIX32 " diff=%.2f",
        target_h, target_l, difficulty);
    if (!mc3_write_register(0, MC3_WORK_TARGET_L, target_l, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_WORK_TARGET_H, target_h, MC3_CHIP_NUM_ALL)) {
        ESP_LOGE(TAG, "Failed broadcasting MC3 work target 0x%016" PRIX64, target);
    }

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        uint32_t readback[2] = {0};
        if (!mc3_read_register_block(chip_id, MC3_WORK_TARGET_L, readback, 2)) {
            ESP_LOGE(TAG, "Failed reading WORK_TARGET from chip %u", chip_id);
            continue;
        }

        uint64_t readback_target = ((uint64_t)readback[1] << 32) | readback[0];
        double effective_difficulty = mc3_work_target_effective_difficulty(readback_target);
        if (readback_target != target) {
            ESP_LOGE(TAG, "WORK_TARGET chip=%u mismatch expected=0x%016" PRIX64
                " readback=0x%016" PRIX64 " effective_diff=%.6f requested_diff=%.6f",
                chip_id, target, readback_target, effective_difficulty, difficulty);
            continue;
        }

        ESP_LOGI(TAG, "WORK_TARGET chip=%u readback=0x%016" PRIX64
            " effective_diff=%.6f requested_diff=%.6f",
            chip_id, readback_target, effective_difficulty, difficulty);
    }
}

static uint32_t mc3_spdlog_timer_value(uint16_t timer_count)
{
    return MC3_SPDLOG_TIMER_ENABLE | timer_count;
}

static double mc3_spdlog_runtime_seconds(uint16_t timer_count)
{
    return ((double)timer_count * 1048576.0) / MC3_SPDLOG_XCLK_HZ;
}

static uint8_t mc3_spdlog_leading_zeros(void)
{
    if (MC3_SPDLOG_CORE_TARGET == 0) {
        return 24;
    }

    uint8_t leading_zeros = 24;
    uint16_t value = MC3_SPDLOG_CORE_TARGET + 1;
    while (value > 1) {
        leading_zeros++;
        value >>= 1;
    }
    return leading_zeros;
}

static void mc3_start_spdlog_window(uint8_t start_chip_id, uint8_t chip_num, uint16_t timer_count)
{
    mc3_write_register(start_chip_id, MC3_GLOBAL_SPD, MC3_DEFAULT_GLOBAL_SPD_VALUE, chip_num);
    mc3_write_register(start_chip_id, MC3_WORK_CFG, MC3_DEFAULT_WORK_CFG_SPDLOG | MC3_SPDLOG_CORE_TARGET, chip_num);
    mc3_write_register(start_chip_id, MC3_SPDLOG_RST, 0x00000001, chip_num);
    mc3_write_register(start_chip_id, MC3_SPDLOG_TIMER, mc3_spdlog_timer_value(timer_count), chip_num);
    mc3_spdlog_started = true;
    mc3_spdlog_start_us = esp_timer_get_time();

    pthread_mutex_lock(&mc3_core_scan_lock);
    if (mc3_core_scan_status.state == ASIC_TUNING_MEASURING) {
        mc3_core_scan_start_us = mc3_spdlog_start_us;
        mc3_core_scan_status.progress_percent = 5;
    }
    pthread_mutex_unlock(&mc3_core_scan_lock);
}

static void mc3_start_spdlog(uint8_t start_chip_id, uint8_t chip_num)
{
    uint16_t timer_count = mc3_get_core_scan_state() == ASIC_TUNING_MEASURING
        ? MC3_TUNING_SPDLOG_TIMER_COUNT
        : MC3_SPDLOG_TIMER_COUNT;
    mc3_start_spdlog_window(start_chip_id, chip_num, timer_count);
}

static void mc3_report_hashrate_result(GlobalState *GLOBAL_STATE, uint8_t chip_id, register_type_t register_type, double hashrate_hs)
{
    if (register_type == REGISTER_HASHRATE &&
        GLOBAL_STATE->HASHRATE_MONITOR_MODULE.is_initialized) {
        hashrate_monitor_set_hashrate(GLOBAL_STATE, chip_id, (float)(hashrate_hs / 1e9), esp_timer_get_time());
    }
}

static bool mc3_read_spdlog_chip(GlobalState *GLOBAL_STATE, uint8_t chip_id,
    uint32_t *passed_out, uint32_t *failed_out, double *pass_hashrate_ghs_out)
{
    uint32_t work_cfg = 0;
    uint32_t timer = 0;
    uint32_t passed = 0;
    uint32_t failed = 0;

    if (!mc3_read_register(chip_id, MC3_WORK_CFG, &work_cfg) ||
        !mc3_read_register(chip_id, MC3_SPDLOG_TIMER, &timer) ||
        !mc3_read_register(chip_id, MC3_SPDLOG_PASS, &passed) ||
        !mc3_read_register(chip_id, MC3_SPDLOG_FAIL, &failed)) {
        ESP_LOGW(TAG, "Failed reading MC3 SPDLOG chip=%u", chip_id);
        return false;
    }

    uint16_t timer_count = timer & 0xFFFF;
    double runtime_seconds = mc3_spdlog_runtime_seconds(timer_count);
    if (runtime_seconds <= 0.0) {
        ESP_LOGW(TAG, "Invalid MC3 SPDLOG timer count chip=%u", chip_id);
        return false;
    }
    double work_per_count = (double)(1ULL << mc3_spdlog_leading_zeros());
    double pass_hashrate_hs = work_per_count * (double)passed / runtime_seconds;

    mc3_report_hashrate_result(GLOBAL_STATE, chip_id, REGISTER_HASHRATE, pass_hashrate_hs);
    *passed_out = passed;
    *failed_out = failed;
    *pass_hashrate_ghs_out = pass_hashrate_hs / 1e9;
    return true;
}

static bool mc3_core_scan_is_active(asic_tuning_state_t state)
{
    return state == ASIC_TUNING_QUEUED ||
        state == ASIC_TUNING_MEASURING ||
        state == ASIC_TUNING_READING;
}

static asic_tuning_state_t mc3_get_core_scan_state(void)
{
    pthread_mutex_lock(&mc3_core_scan_lock);
    asic_tuning_state_t state = mc3_core_scan_status.state;
    pthread_mutex_unlock(&mc3_core_scan_lock);
    return state;
}

static void mc3_set_core_scan_state(asic_tuning_state_t state, uint8_t progress, const char *message)
{
    pthread_mutex_lock(&mc3_core_scan_lock);
    mc3_core_scan_status.state = state;
    mc3_core_scan_status.progress_percent = progress;
    snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message), "%s", message);
    pthread_mutex_unlock(&mc3_core_scan_lock);
}

bool MC3_start_core_scan(void)
{
    pthread_mutex_lock(&mc3_core_scan_lock);

    if (mc3_core_scan_is_active(mc3_core_scan_status.state)) {
        pthread_mutex_unlock(&mc3_core_scan_lock);
        return false;
    }

    if (mc3_chip_count == 0 || mc3_chip_count > ASIC_TUNING_MAX_CHIPS) {
        mc3_core_scan_status.state = ASIC_TUNING_ERROR;
        mc3_core_scan_status.progress_percent = 0;
        snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message),
            "Unsupported MC3 chip count: %u", mc3_chip_count);
        pthread_mutex_unlock(&mc3_core_scan_lock);
        return false;
    }

    memset(mc3_core_scan_results, 0, sizeof(mc3_core_scan_results));
    mc3_core_scan_status.supported = true;
    mc3_core_scan_status.validated = false;
    mc3_core_scan_status.state = ASIC_TUNING_QUEUED;
    mc3_core_scan_status.progress_percent = 0;
    mc3_core_scan_status.chip_count = mc3_chip_count;
    mc3_core_scan_status.core_count = ASIC_TUNING_MAX_CORES;
    mc3_core_scan_status.leading_zeros = mc3_spdlog_leading_zeros();
    mc3_core_scan_status.runtime_seconds = mc3_spdlog_runtime_seconds(MC3_TUNING_SPDLOG_TIMER_COUNT);
    uint32_t scan_id = ++mc3_core_scan_status.scan_id;
    snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message), "Waiting for active work");
    mc3_core_scan_start_us = 0;

    pthread_mutex_unlock(&mc3_core_scan_lock);
    ESP_LOGI(TAG, "Queued MC3 per-core tuning scan %" PRIu32, scan_id);
    return true;
}

void MC3_get_core_scan_status(asic_tuning_status_t *status)
{
    pthread_mutex_lock(&mc3_core_scan_lock);
    *status = mc3_core_scan_status;

    if (status->state == ASIC_TUNING_MEASURING && mc3_core_scan_start_us > 0) {
        int64_t elapsed_us = esp_timer_get_time() - mc3_core_scan_start_us;
        int64_t scan_time_us = (int64_t)(status->runtime_seconds * 1000000.0) + MC3_TUNING_SETTLE_TIME_US;
        if (elapsed_us > 0 && scan_time_us > 0) {
            int64_t measurement_progress = (elapsed_us * 65) / scan_time_us;
            status->progress_percent = 5 + (uint8_t)(measurement_progress > 65 ? 65 : measurement_progress);
        }
    }

    pthread_mutex_unlock(&mc3_core_scan_lock);
}

bool MC3_get_core_scan_chip_result(uint8_t chip_id, asic_tuning_chip_result_t *result)
{
    pthread_mutex_lock(&mc3_core_scan_lock);
    bool available = chip_id < mc3_core_scan_status.chip_count &&
        (mc3_core_scan_status.state == ASIC_TUNING_COMPLETE || mc3_core_scan_status.state == ASIC_TUNING_ERROR);
    if (available) {
        *result = mc3_core_scan_results[chip_id];
    }
    pthread_mutex_unlock(&mc3_core_scan_lock);
    return available;
}

static void mc3_begin_core_scan(void)
{
    mc3_start_spdlog_window(0, MC3_CHIP_NUM_ALL, MC3_TUNING_SPDLOG_TIMER_COUNT);

    pthread_mutex_lock(&mc3_core_scan_lock);
    mc3_core_scan_start_us = mc3_spdlog_start_us;
    mc3_core_scan_status.state = ASIC_TUNING_MEASURING;
    mc3_core_scan_status.progress_percent = 5;
    snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message), "Measuring core PASS counters");
    pthread_mutex_unlock(&mc3_core_scan_lock);

    ESP_LOGI(TAG, "Starting %.3f second MC3 per-core tuning scan using SPDLOG banks 0x%08" PRIX32
        "/0x%08" PRIX32,
        mc3_spdlog_runtime_seconds(MC3_TUNING_SPDLOG_TIMER_COUNT),
        (uint32_t)MC3_CORE_SPDLOG_BANK0, (uint32_t)MC3_CORE_SPDLOG_BANK1);
}

static bool mc3_read_core_spdlog_bank(uint8_t chip_id, uint32_t base, uint16_t logical_core_offset,
    uint32_t *pass_counts)
{
    for (uint16_t offset = 0; offset < MC3_CORE_SPDLOG_BANK_CORE_COUNT; offset += MC3_CORE_PASS_READ_CHUNK) {
        uint16_t remaining = MC3_CORE_SPDLOG_BANK_CORE_COUNT - offset;
        uint8_t count = remaining < MC3_CORE_PASS_READ_CHUNK ? remaining : MC3_CORE_PASS_READ_CHUNK;
        uint32_t reg = base + ((uint32_t)offset * MC3_CORE_SPDLOG_REGISTER_STRIDE);
        uint32_t *packed_counters = pass_counts + logical_core_offset + offset;
        bool read_ok = false;

        for (int attempt = 0; attempt < 2 && !read_ok; attempt++) {
            read_ok = mc3_read_register_block(chip_id, reg, packed_counters, count);
        }

        if (!read_ok) {
            ESP_LOGW(TAG, "Failed reading per-core SPDLOG chip=%u base=0x%08" PRIX32 " core=%u",
                chip_id, base, logical_core_offset + offset);
            return false;
        }

        for (uint8_t i = 0; i < count; i++) {
            packed_counters[i] &= MC3_CORE_SPDLOG_PASS_MASK;
        }
    }

    return true;
}

static bool mc3_read_core_pass_counts(uint8_t chip_id, uint32_t *pass_counts)
{
    return mc3_read_core_spdlog_bank(chip_id, MC3_CORE_SPDLOG_BANK0, 0, pass_counts) &&
        mc3_read_core_spdlog_bank(chip_id, MC3_CORE_SPDLOG_BANK1,
            MC3_CORE_SPDLOG_BANK_CORE_COUNT, pass_counts);
}

static uint64_t mc3_sum_core_pass_counts(const uint32_t *pass_counts)
{
    uint64_t sum = 0;
    for (uint16_t core_id = 0; core_id < ASIC_TUNING_MAX_CORES; core_id++) {
        sum += pass_counts[core_id];
    }
    return sum;
}

static bool mc3_core_pass_sum_matches(uint64_t core_pass_sum, uint32_t global_pass)
{
    if (global_pass == 0) {
        return false;
    }

    uint64_t difference = core_pass_sum > global_pass
        ? core_pass_sum - global_pass
        : global_pass - core_pass_sum;
    uint64_t tolerance = global_pass / 20;
    if (tolerance < 4) {
        tolerance = 4;
    }
    return difference <= tolerance;
}

static bool mc3_collect_core_scan(GlobalState *GLOBAL_STATE)
{
    bool all_validated = true;
    mc3_set_core_scan_state(ASIC_TUNING_READING, 75, "Reading per-core PASS counters");

    for (uint8_t chip_id = 0; chip_id < mc3_chip_count; chip_id++) {
        asic_tuning_chip_result_t chip_result = { .chip_id = chip_id };

        if (!mc3_read_spdlog_chip(GLOBAL_STATE, chip_id, &chip_result.global_pass,
                &chip_result.global_fail, &chip_result.global_hashrate_ghs)) {
            mc3_set_core_scan_state(ASIC_TUNING_ERROR, 75, "Could not read global MC3 SPDLOG counters");
            return false;
        }

        if (!mc3_read_core_pass_counts(chip_id, chip_result.core_pass_counts)) {
            mc3_set_core_scan_state(ASIC_TUNING_ERROR, 75, "Could not read MC3 per-core SPDLOG counters");
            return false;
        }

        chip_result.core_pass_sum = mc3_sum_core_pass_counts(chip_result.core_pass_counts);

        if (chip_result.global_pass > 0 && chip_result.core_pass_sum == 0) {
            char message[ASIC_TUNING_MESSAGE_LENGTH];
            snprintf(message, sizeof(message), "ASIC %u per-core PASS bank returned all zeros", chip_id + 1);
            ESP_LOGE(TAG, "%s while global PASS=%" PRIu32, message, chip_result.global_pass);
            mc3_set_core_scan_state(ASIC_TUNING_ERROR, 75, message);
            return false;
        }

        uint64_t global_pass = chip_result.global_pass;
        chip_result.validated = mc3_core_pass_sum_matches(chip_result.core_pass_sum, chip_result.global_pass);
        all_validated = all_validated && chip_result.validated;

        pthread_mutex_lock(&mc3_core_scan_lock);
        mc3_core_scan_results[chip_id] = chip_result;
        mc3_core_scan_status.progress_percent = 75 + (((chip_id + 1) * 25) / mc3_chip_count);
        pthread_mutex_unlock(&mc3_core_scan_lock);

        double match_percent = global_pass > 0
            ? ((double)chip_result.core_pass_sum * 100.0) / (double)global_pass
            : 0.0;
        ESP_LOGI(TAG, "Core scan chip=%u global=%" PRIu32 " core_sum=%" PRIu64 " match=%.2f%%",
            chip_id, chip_result.global_pass, chip_result.core_pass_sum, match_percent);
    }

    pthread_mutex_lock(&mc3_core_scan_lock);
    mc3_core_scan_status.validated = all_validated;
    mc3_core_scan_status.state = ASIC_TUNING_COMPLETE;
    mc3_core_scan_status.progress_percent = 100;
    snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message), "%s",
        all_validated ? "Core scan complete" : "Core PASS sum did not match global PASS");
    pthread_mutex_unlock(&mc3_core_scan_lock);
    return true;
}

static void mc3_write_version_bases(bm_job *next_bm_job, uint8_t chip_count)
{
    uint8_t start_bit = 0;
    if (!mc3_version_start_bit(next_bm_job->version_mask, &start_bit)) {
        return;
    }

    uint32_t version_base = next_bm_job->version;
    uint32_t version_space = 1U << MC3_DEFAULT_VERSION_MASK_BITS;
    uint32_t stride = version_space / chip_count;
    uint32_t rolling_mask = mc3_version_rolling_mask(start_bit);

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        uint32_t rolling_value = version_base + ((uint32_t)chip_id * stride << start_bit);
        uint32_t chip_version_base = (version_base & ~rolling_mask) | (rolling_value & rolling_mask);
        ESP_LOGI(TAG, "Writing V_BASE chip=%u value=0x%08" PRIX32, chip_id, chip_version_base);
        mc3_write_register(chip_id, MC3_V_BASE, chip_version_base, 0);
    }
}

static void mc3_write_v_work(bm_job *next_bm_job, uint8_t job_id)
{
    mc3_write_register(0, MC3_V_WORK_INIT, 0x00000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_WORK_NTIME, mc3_bswap32(next_bm_job->ntime), MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_WORK_NBITS, mc3_bswap32(next_bm_job->target), MC3_CHIP_NUM_ALL);

    for (uint8_t i = 0; i < 8; i++) {
        mc3_write_register(0, MC3_V_WORK_MERKLE_ROOT + i * 4, mc3_bm_field_word(next_bm_job->merkle_root, i), MC3_CHIP_NUM_ALL);
    }

    for (uint8_t i = 0; i < 8; i++) {
        mc3_write_register(0, MC3_V_WORK_PREVIOUS_HASH + i * 4, mc3_bm_field_word(next_bm_job->prev_block_hash, i), MC3_CHIP_NUM_ALL);
    }

    mc3_write_register(0, MC3_V_WORK_NOTE, job_id, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_WORK_NOTE + 4, 0x00000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_WORK_NOTE + 8, 0x00000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_V_WORK_NOTE + 12, 0x00000000, MC3_CHIP_NUM_ALL);
}

static bool mc3_program_qualification_work(float frequency_mhz)
{
    uint8_t chip_count = mc3_chip_count;
    if (chip_count == 0 || chip_count > MC3_QUALIFICATION_MAX_CHIPS) {
        ESP_LOGE(TAG, "Cannot start MC3 qualification with %u chip(s)", chip_count);
        return false;
    }

    bm_job qualification_job = {
        .version = MC3_QUALIFICATION_VERSION,
        .version_mask = MC3_QUALIFICATION_VERSION_MASK,
        .ntime = MC3_QUALIFICATION_NTIME,
        .target = MC3_QUALIFICATION_NBITS,
        .pool_diff = MC3_QUALIFICATION_POOL_DIFFICULTY,
    };

    const mc3_pll_config_t *config = mc3_get_pll_config(frequency_mhz);
    if (!mc3_write_mining_config(
            mc3_rolltime_for_frequency(config->frequency_mhz),
            qualification_job.version_mask)) {
        return false;
    }

    mc3_write_work_target(qualification_job.pool_diff, chip_count);
    mc3_write_version_bases(&qualification_job, chip_count);
    mc3_write_v_work(&qualification_job, 0);
    return true;
}

static bool mc3_pending_push(const task_result *new_result)
{
    pthread_mutex_lock(&mc3_pending_lock);
    if (mc3_pending_count >= MC3_PENDING_RESULTS_SIZE) {
        pthread_mutex_unlock(&mc3_pending_lock);
        ESP_LOGW(TAG, "MC3 result queue full, dropping nonce");
        return false;
    }

    uint8_t tail = (mc3_pending_head + mc3_pending_count) % MC3_PENDING_RESULTS_SIZE;
    mc3_pending_results[tail] = *new_result;
    mc3_pending_count++;
    pthread_mutex_unlock(&mc3_pending_lock);
    return true;
}

static bool mc3_pending_pop(task_result *out_result)
{
    if (mc3_pending_count == 0) {
        return false;
    }

    pthread_mutex_lock(&mc3_pending_lock);
    if (mc3_pending_count == 0) {
        pthread_mutex_unlock(&mc3_pending_lock);
        return false;
    }

    *out_result = mc3_pending_results[mc3_pending_head];
    mc3_pending_head = (mc3_pending_head + 1) % MC3_PENDING_RESULTS_SIZE;
    mc3_pending_count--;
    pthread_mutex_unlock(&mc3_pending_lock);
    return true;
}

static bool mc3_nonce_record_seen(uint8_t chip_id, uint8_t index, uint32_t metadata, uint32_t nonce, uint32_t version)
{
    uint32_t *seen = mc3_seen_nonce_words[chip_id][index];
    if (seen[0] == metadata && seen[1] == nonce && seen[2] == version) {
        return true;
    }

    seen[0] = metadata;
    seen[1] = nonce;
    seen[2] = version;
    return false;
}

static bool mc3_read_nonce_buffer(uint8_t chip_id, uint32_t words[MC3_NONCE_BUFFER_WORDS])
{
    for (uint8_t i = 0; i < MC3_NONCE_BUFFER_WORDS; i++) {
        if (!mc3_read_register(chip_id, MC3_NONCE_BUFFER + i * 4, &words[i])) {
            ESP_LOGW(TAG, "Failed reading MC3 nonce buffer chip=%u word=%u", chip_id, i);
            return false;
        }
    }

    return true;
}

static bool mc3_wait_register(uint8_t chip_id, uint32_t reg, uint32_t expected, uint16_t timeout_ms)
{
    uint32_t value = 0;
    int iterations = timeout_ms / 10;
    if (iterations <= 0) {
        iterations = 1;
    }

    for (int i = 0; i < iterations; i++) {
        if (mc3_read_register(chip_id, reg, &value) && value == expected) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "Register 0x%08" PRIX32 " stayed 0x%08" PRIX32 ", expected 0x%08" PRIX32,
        reg, value, expected);
    return false;
}

static void mc3_setup_thermal_sensor(void)
{
    mc3_write_register(0, MC3_THERMAL_SDIF_ENABLE, 0x00000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_THERMAL_CLOCK, 0x01010000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_THERMAL_COMMAND, 0x89000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_THERMAL_COMMAND, 0x8D000200, MC3_CHIP_NUM_ALL);
    mc3_wait_register(0, MC3_THERMAL_STATUS, 0x00000000, 500);
}

static bool mc3_trigger_thermal_conversion(void)
{
    mc3_write_register(0, MC3_THERMAL_COMMAND, 0x88000105, MC3_CHIP_NUM_ALL);
    return mc3_wait_register(0, MC3_THERMAL_DONE, 0x00000001, 500);
}

static void mc3_setup_vdd_voltage_sensor(void)
{
    uint32_t channel_mask = (1U << MC3_VOLTAGE_VDD_CHANNEL_A) | (1U << MC3_VOLTAGE_VDD_CHANNEL_B);

    mc3_write_register(0, MC3_VOLTAGE_SDIF_ENABLE, 0x00000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_VOLTAGE_CLOCK, MC3_VOLTAGE_CLOCK_DEFAULT, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_VOLTAGE_COMMAND, 0x89000000, MC3_CHIP_NUM_ALL);
    mc3_write_register(0, MC3_VOLTAGE_COMMAND, 0x8D000040, MC3_CHIP_NUM_ALL);
    mc3_wait_register(0, MC3_VOLTAGE_STATUS, 0x00000000, 500);
    mc3_write_register(0, MC3_VOLTAGE_COMMAND, 0x8C100000 | channel_mask, MC3_CHIP_NUM_ALL);
    mc3_wait_register(0, MC3_VOLTAGE_STATUS, 0x00000000, 500);
}

static bool mc3_trigger_voltage_conversion(void)
{
    mc3_write_register(0, MC3_VOLTAGE_COMMAND, 0x88000505, MC3_CHIP_NUM_ALL);
    return mc3_wait_register(0, MC3_VOLTAGE_DONE, 0x00000001, 500);
}

static bool mc3_get_active_job_fields(GlobalState *GLOBAL_STATE, uint8_t job_id, uint32_t *version, uint32_t *ntime)
{
    bool found = false;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    if (GLOBAL_STATE->valid_jobs[job_id] != 0 && GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] != NULL) {
        *version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version;
        *ntime = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->ntime;
        found = true;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    return found;
}

static uint32_t mc3_result_ntime(uint32_t job_ntime, uint16_t ntime_offset)
{
    if (job_ntime == 0) {
        return 0;
    }

    return job_ntime + ntime_offset;
}

static void mc3_enqueue_nonce_results(GlobalState *GLOBAL_STATE, uint8_t chip_id, const uint32_t words[MC3_NONCE_BUFFER_WORDS])
{
    uint8_t job_id = words[0] & 0x7F;

    if (job_id >= 128) {
        ESP_LOGW(TAG, "MC3 nonce buffer has invalid job id %u", job_id);
        return;
    }

    for (uint8_t index = 0; index < MC3_VERSION_ROLLING_NONCE_COUNT; index++) {
        uint8_t offset = MC3_NONCE_BUFFER_NOTE_WORDS + index * 3;
        uint32_t metadata = words[offset];
        uint32_t nonce = words[offset + 1];
        uint32_t version = words[offset + 2];

        if ((metadata & MC3_NONCE_VALID_BIT) == 0) {
            continue;
        }
        if (mc3_nonce_record_seen(chip_id, index, metadata, nonce, version)) {
            continue;
        }

        uint16_t ntime_offset = metadata & 0xFFFF;
        uint32_t job_version = 0;
        uint32_t job_ntime = 0;
        if (!mc3_get_active_job_fields(GLOBAL_STATE, job_id, &job_version, &job_ntime)) {
            ESP_LOGW(TAG, "MC3 nonce for inactive job_id=%u", job_id);
            continue;
        }

        uint32_t ntime = mc3_result_ntime(job_ntime, ntime_offset);

        task_result new_result = {0};
        new_result.job_id = job_id;
        new_result.nonce = mc3_bswap32(nonce);
        new_result.rolled_version = version;
        new_result.ntime = ntime;
        new_result.asic_nr = chip_id;
        new_result.core_id = (metadata >> 16) & 0x7;
        new_result.small_core_id = index;
        new_result.timestamp_us = esp_timer_get_time();

        ESP_LOGI(TAG,
            "MC3 nonce chip=%u slot=%u job=%u core=%u ntime_offset=%u nonce=0x%08" PRIX32 " raw_version=0x%08" PRIX32 " rolled_version=0x%08" PRIX32,
            chip_id, index, job_id, new_result.core_id, ntime_offset, new_result.nonce, version, new_result.rolled_version);
        mc3_pending_push(&new_result);
    }
}

static void mc3_poll_nonce_chip(GlobalState *GLOBAL_STATE, uint8_t chip_id)
{
    uint32_t words[MC3_NONCE_BUFFER_WORDS] = {0};
    uint32_t nonce_update = 0;

    if (!mc3_read_register(chip_id, MC3_NONCE_UPDATE, &nonce_update)) {
        ESP_LOGW(TAG, "Failed reading MC3 nonce status chip=%u", chip_id);
        return;
    }
    if ((nonce_update & MC3_NONCE_READY_BIT) == 0) {
        return;
    }

    if (!mc3_write_register(chip_id, MC3_NONCE_UPDATE, MC3_NONCE_UPDATE_BIT, 0)) {
        ESP_LOGW(TAG, "Failed updating MC3 nonce buffer chip=%u", chip_id);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    if (mc3_read_nonce_buffer(chip_id, words)) {
        mc3_enqueue_nonce_results(GLOBAL_STATE, chip_id, words);
    }
}

static uint8_t mc3_header_mode(uint32_t header)
{
    return (header >> 8) & 0x0F;
}

static uint8_t mc3_build_init_packet(uint8_t *buffer, uint8_t first_chip_id)
{
    uint32_t header = mc3_build_header(0, MC3_CHIP_NUM_ALL, MC3_MODE_INIT, 1);
    uint32_t first_chip_id_word = (uint32_t)first_chip_id << 24;

    mc3_encode_field(buffer, header, false);
    mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES, 0x00000000, false);
    mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES * 2, first_chip_id_word, true);

    return MC3_ULINK_FIELD_BYTES * 3;
}

static uint8_t mc3_build_write_packet(uint8_t *buffer, uint8_t chip_id, uint32_t reg, const uint32_t *values, uint8_t value_count, uint8_t chip_num)
{
    uint32_t header = mc3_build_header(chip_id, chip_num, MC3_MODE_WRITE, value_count);

    mc3_encode_field(buffer, header, false);
    mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES, reg, false);

    for (int i = 0; i < value_count; i++) {
        mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES * (i + 2), values[i], i == value_count - 1);
    }

    return MC3_ULINK_FIELD_BYTES * (value_count + 2);
}

static uint8_t mc3_build_read_packet(uint8_t *buffer, uint8_t chip_id, uint32_t reg, uint8_t data_length)
{
    uint32_t header = mc3_build_header(chip_id, 0, MC3_MODE_READ, data_length);

    mc3_encode_field(buffer, header, false);
    mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES, reg, false);
    mc3_encode_field(buffer + MC3_ULINK_FIELD_BYTES * 2, 0x00000000, true);

    return MC3_ULINK_FIELD_BYTES * 3;
}

static bool mc3_parse_init_response(const uint8_t *response, uint16_t response_len, uint8_t *next_chip_id)
{
    if (response_len < MC3_INIT_RESPONSE_BYTES) {
        return false;
    }

    for (int offset = 0; offset <= response_len - MC3_INIT_RESPONSE_BYTES; offset++) {
        mc3_field_t header = mc3_decode_field(response + offset);
        mc3_field_t data = mc3_decode_field(response + offset + MC3_ULINK_FIELD_BYTES * 2);

        if (!header.ecc_ok) {
            continue;
        }
        if ((header.word >> 24) != MC3_START_FLAG || mc3_header_mode(header.word) != MC3_MODE_INIT) {
            continue;
        }

        if (!data.ecc_ok) {
            ESP_LOGW(TAG, "MC3 init data ECC mismatch: word=0x%08" PRIX32 " ecc=0x%02X", data.word, data.ecc);
        }

        *next_chip_id = (data.word >> 24) & 0xFF;
        return true;
    }

    return false;
}

static bool mc3_parse_read_response(const uint8_t *response, uint16_t response_len, uint8_t expected_chip_id,
    uint32_t expected_register, uint8_t expected_length, uint32_t *data)
{
    uint8_t field_count = MC3_ULINK_READ_META_FIELDS + expected_length;
    uint16_t packet_len = field_count * MC3_ULINK_FIELD_BYTES;

    if (response_len < packet_len) {
        return false;
    }

    for (int offset = 0; offset <= response_len - packet_len; offset++) {
        mc3_field_t header = mc3_decode_field(response + offset);
        mc3_field_t reg = mc3_decode_field(response + offset + MC3_ULINK_FIELD_BYTES);
        mc3_field_t meta = mc3_decode_field(response + offset + MC3_ULINK_FIELD_BYTES * 2);

        if (!header.ecc_ok || !reg.ecc_ok || !meta.ecc_ok) {
            continue;
        }
        if ((header.word >> 24) != MC3_START_FLAG || mc3_header_mode(header.word) != MC3_MODE_READ) {
            continue;
        }
        if (reg.word != expected_register) {
            continue;
        }
        if ((meta.word >> 16) != 0xFFAA || ((meta.word >> 8) & 0xFF) != expected_chip_id || (meta.word & 0xFF) != expected_length) {
            continue;
        }

        for (int i = 0; i < expected_length; i++) {
            mc3_field_t field = mc3_decode_field(response + offset + MC3_ULINK_FIELD_BYTES * (i + 3));
            if (!field.ecc_ok) {
                return false;
            }
            data[i] = field.word;
        }
        return true;
    }

    return false;
}

static int16_t mc3_transact(uint8_t *packet, uint16_t packet_len, uint8_t *response, uint16_t response_len)
{
    pthread_mutex_lock(&mc3_serial_lock);
    SERIAL_clear_buffer();
    SERIAL_send(packet, packet_len, MC3_SERIALTX_DEBUG);
    vTaskDelay(pdMS_TO_TICKS(MC3_ULINK_RESPONSE_DELAY_MS));
    int16_t received = SERIAL_rx(response, response_len, 20);
    pthread_mutex_unlock(&mc3_serial_lock);
    return received;
}

static bool mc3_write_register(uint8_t chip_id, uint32_t reg, uint32_t value, uint8_t chip_num)
{
    uint8_t packet[MC3_ULINK_FIELD_BYTES * 3] = {0};
    uint8_t response[MC3_ULINK_FIELD_BYTES * 3] = {0};
    uint8_t packet_len = mc3_build_write_packet(packet, chip_id, reg, &value, 1, chip_num);

    int16_t received = mc3_transact(packet, packet_len, response, sizeof(response));
    if (received < 0) {
        ESP_LOGW(TAG, "Failed writing register 0x%08" PRIX32, reg);
        return false;
    }
    return true;
}

static bool mc3_read_register(uint8_t chip_id, uint32_t reg, uint32_t *value)
{
    uint8_t packet[MC3_ULINK_FIELD_BYTES * 3] = {0};
    uint8_t response[MC3_ULINK_FIELD_BYTES * 4] = {0};
    uint8_t packet_len = mc3_build_read_packet(packet, chip_id, reg, 1);

    int16_t received = mc3_transact(packet, packet_len, response, sizeof(response));
    if (received <= 0) {
        ESP_LOGW(TAG, "No response reading register 0x%08" PRIX32, reg);
        return false;
    }

    return mc3_parse_read_response(response, received, chip_id, reg, 1, value);
}

static bool mc3_read_register_block(uint8_t chip_id, uint32_t reg, uint32_t *values, uint8_t value_count)
{
    if (value_count == 0 || value_count > MC3_CORE_PASS_READ_CHUNK) {
        return false;
    }

    uint8_t packet[MC3_ULINK_FIELD_BYTES * 3] = {0};
    uint8_t response[MC3_ULINK_FIELD_BYTES * (MC3_ULINK_READ_META_FIELDS + MC3_CORE_PASS_READ_CHUNK)] = {0};
    uint8_t packet_len = mc3_build_read_packet(packet, chip_id, reg, value_count);
    uint16_t response_len = MC3_ULINK_FIELD_BYTES * (MC3_ULINK_READ_META_FIELDS + value_count);

    int16_t received = mc3_transact(packet, packet_len, response, response_len);
    if (received <= 0) {
        ESP_LOGW(TAG, "No response reading %u registers from 0x%08" PRIX32, value_count, reg);
        return false;
    }

    return mc3_parse_read_response(response, received, chip_id, reg, value_count, values);
}

uint8_t MC3_init(void * pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    uint8_t packet[MC3_INIT_RESPONSE_BYTES] = {0};
    uint8_t response[64] = {0};
    uint8_t next_chip_id = 0;

    ESP_LOGI(TAG, "Initializing MC3 chain");

    uint8_t packet_len = mc3_build_init_packet(packet, 0);
    int16_t received = mc3_transact(packet, packet_len, response, sizeof(response));
    if (received <= 0) {
        ESP_LOGE(TAG, "No MC3 init response");
        mc3_chip_count = 0;
        return 0;
    }

    if (!mc3_parse_init_response(response, received, &next_chip_id)) {
        ESP_LOGE(TAG, "Invalid MC3 init response");
        ESP_LOG_BUFFER_HEX(TAG, response, received);
        mc3_chip_count = 0;
        return 0;
    }

    mc3_chip_count = next_chip_id;
    if (mc3_chip_count == 0) {
        mc3_chip_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    }

    pthread_mutex_lock(&mc3_core_scan_lock);
    if (mc3_core_scan_is_active(mc3_core_scan_status.state)) {
        mc3_core_scan_status.state = ASIC_TUNING_ERROR;
        mc3_core_scan_status.progress_percent = 0;
        snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message),
            "Core scan interrupted by ASIC reset");
    }
    mc3_core_scan_status.chip_count = mc3_chip_count;
    mc3_core_scan_status.leading_zeros = mc3_spdlog_leading_zeros();
    mc3_core_scan_status.runtime_seconds = mc3_spdlog_runtime_seconds(MC3_TUNING_SPDLOG_TIMER_COUNT);
    if (mc3_core_scan_status.state == ASIC_TUNING_IDLE) {
        snprintf(mc3_core_scan_status.message, sizeof(mc3_core_scan_status.message), "Ready");
    }
    pthread_mutex_unlock(&mc3_core_scan_lock);

    ESP_LOGI(TAG, "MC3 init response: next_chip_id=%u using_chip_count=%u", next_chip_id, mc3_chip_count);

    float startup_frequency = MC3_send_hash_frequency(MC3_STARTUP_FREQUENCY_MHZ);
    if (startup_frequency <= 0.0f) {
        ESP_LOGE(TAG, "Failed to start MC3 PLLs at %u MHz", MC3_STARTUP_FREQUENCY_MHZ);
        mc3_chip_count = 0;
        return 0;
    }
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = startup_frequency;

    mc3_setup_vdd_voltage_sensor();

    float startup_voltages_mv[ASIC_TUNING_MAX_CHIPS] = {0};
    uint8_t voltage_count = MC3_read_vdd_voltages(startup_voltages_mv, ASIC_TUNING_MAX_CHIPS);
    if (voltage_count == 0) {
        ESP_LOGW(TAG, "No MC3 VDD measurements available before frequency ramp");
    } else {
        for (uint8_t chip_id = 0; chip_id < voltage_count; chip_id++) {
            ESP_LOGI(TAG, "Pre-ramp VDD: chip %u %.1f mV", chip_id, startup_voltages_mv[chip_id]);
        }
    }

    mc3_setup_thermal_sensor();

    return mc3_chip_count;
}

void MC3_send_work(void * pvParameters, bm_job * next_bm_job)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    uint8_t chip_count = mc3_chip_count;
    uint32_t rolltime = mc3_current_rolltime(GLOBAL_STATE);

    if (chip_count == 0) {
        chip_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    }
    if (chip_count == 0) {
        ESP_LOGE(TAG, "Cannot send MC3 work without a chip count");
        free_bm_job(next_bm_job);
        return;
    }

    pthread_mutex_lock(&mc3_work_lock);

    mc3_job_id = (mc3_job_id + 1) % 128;
    ESP_LOGI(TAG, "Writing MC3 work job_id=%u chips=%u", mc3_job_id, chip_count);

    pthread_mutex_lock(&mc3_pending_lock);
    mc3_pending_head = 0;
    mc3_pending_count = 0;
    pthread_mutex_unlock(&mc3_pending_lock);
    memset(mc3_seen_nonce_words, 0, sizeof(mc3_seen_nonce_words));

    if (!mc3_write_mining_config(rolltime, next_bm_job->version_mask)) {
        ESP_LOGE(TAG, "Cannot start MC3 work job_id=%u with version mask 0x%08" PRIX32,
            mc3_job_id, next_bm_job->version_mask);
        free_bm_job(next_bm_job);
        pthread_mutex_unlock(&mc3_work_lock);
        return;
    }
    mc3_write_work_target(next_bm_job->pool_diff, chip_count);
    mc3_write_version_bases(next_bm_job, chip_count);
    mc3_store_active_job(GLOBAL_STATE, mc3_job_id, next_bm_job);
    mc3_write_v_work(next_bm_job, mc3_job_id);
    mc3_start_spdlog(0, MC3_CHIP_NUM_ALL);
    pthread_mutex_unlock(&mc3_work_lock);
}

void MC3_set_version_mask(uint32_t version_mask)
{
    uint8_t start_bit = 0;
    if (!mc3_version_start_bit(version_mask, &start_bit)) {
        ESP_LOGE(TAG, "Unsupported MC3 version mask 0x%08" PRIX32, version_mask);
        return;
    }

    ESP_LOGI(TAG, "MC3 version mask 0x%08" PRIX32 " uses start_bit=%u", version_mask, start_bit);
}

int MC3_set_default_baud(void)
{
    return 115200;
}

int MC3_set_max_baud(void)
{
    return MC3_set_default_baud();
}

float MC3_send_hash_frequency(float frequency)
{
    const mc3_pll_config_t *config = mc3_get_pll_config(frequency);
    uint32_t pll_config = mc3_pll_config_value(config);
    uint32_t rolltime = mc3_rolltime_for_frequency(config->frequency_mhz);
    uint8_t chip_count = mc3_chip_count;

    ESP_LOGI(TAG, "Setting Frequency to %u MHz", config->frequency_mhz);
    if (!mc3_write_register(0, MC3_GLOBAL_SPD, MC3_DEFAULT_GLOBAL_SPD_VALUE, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_PLL0_EN, 0x00000000, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_PLL0_CFG, pll_config, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_PLL0_EN, MC3_PLL0_ENABLE, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_PLL0_EN, MC3_DEFAULT_PLL0_ENABLE, MC3_CHIP_NUM_ALL) ||
        !mc3_write_register(0, MC3_ROLLTIME, rolltime, MC3_CHIP_NUM_ALL)) {
        ESP_LOGE(TAG, "Failed writing MC3 PLL configuration for %u MHz", config->frequency_mhz);
        return 0.0f;
    }

    if (chip_count == 0) {
        ESP_LOGE(TAG, "Cannot verify MC3 PLL lock without an enumerated chip count");
        return 0.0f;
    }

    int64_t deadline_us = esp_timer_get_time() + (MC3_PLL_LOCK_TIMEOUT_MS * 1000);
    bool all_locked = false;
    bool chip_locked[MC3_MAX_TRACKED_CHIPS] = {0};

    do {
        all_locked = true;
        for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
            uint32_t pll_en = 0;
            chip_locked[chip_id] = mc3_read_register(chip_id, MC3_PLL0_EN, &pll_en) &&
                                   (pll_en & MC3_PLL_LOCK_BIT) != 0;
            if (!chip_locked[chip_id]) {
                all_locked = false;
            }
        }

        if (!all_locked) {
            vTaskDelay(pdMS_TO_TICKS(MC3_PLL_LOCK_POLL_MS));
        }
    } while (!all_locked && esp_timer_get_time() < deadline_us);

    if (!all_locked) {
        for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
            if (!chip_locked[chip_id]) {
                ESP_LOGE(TAG, "PLL0 failed to lock at %u MHz on chip %u", config->frequency_mhz, chip_id);
            }
        }
        return 0.0f;
    }

    ESP_LOGI(TAG, "PLL0 locked at %u MHz on all %u chips", config->frequency_mhz, chip_count);
    return config->frequency_mhz;
}

void MC3_set_frequency_step_callback(mc3_frequency_step_callback_t callback)
{
    mc3_frequency_step_callback = callback;
}

bool MC3_ramp_hash_frequency(void * pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    PowerManagementModule *power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    const mc3_pll_config_t *target_config = mc3_get_pll_config(power_management->frequency_value);
    int target_index = target_config - PLL_CONFIGS;
    int current_index = -1;

    for (int i = 0; i < sizeof(PLL_CONFIGS) / sizeof(PLL_CONFIGS[0]); i++) {
        if (PLL_CONFIGS[i].frequency_mhz == (uint16_t)(power_management->actual_frequency + 0.5f)) {
            current_index = i;
            break;
        }
    }

    if (current_index == target_index) {
        return true;
    }

    // Keep work programming and SPDLOG reads from interleaving with the
    // multi-register PLL transition when frequency changes while mining.
    pthread_mutex_lock(&mc3_work_lock);

    ESP_LOGI(TAG, "Ramping frequency from %.0f MHz to %u MHz",
        power_management->actual_frequency, target_config->frequency_mhz);

    int direction = current_index < target_index ? 1 : -1;
    int step_index = current_index < 0 ? 0 : current_index + direction;

    while ((direction > 0 && step_index <= target_index) ||
           (direction < 0 && step_index >= target_index)) {
        float actual_frequency = MC3_send_hash_frequency(PLL_CONFIGS[step_index].frequency_mhz);
        if (actual_frequency <= 0.0f) {
            ESP_LOGE(TAG, "Frequency ramp stopped at %.0f MHz; target was %u MHz",
                power_management->actual_frequency, target_config->frequency_mhz);
            pthread_mutex_unlock(&mc3_work_lock);
            return false;
        }

        power_management->actual_frequency = actual_frequency;
        uint32_t settle_delay_ms = actual_frequency > MC3_FREQUENCY_HIGH_THRESHOLD_MHZ
            ? MC3_FREQUENCY_HIGH_RAMP_DELAY_MS
            : MC3_FREQUENCY_RAMP_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(settle_delay_ms));
        if (mc3_frequency_step_callback != NULL) {
            mc3_frequency_step_callback(GLOBAL_STATE, actual_frequency);
        }
        step_index += direction;
    }

    ESP_LOGI(TAG, "Successfully transitioned to %u MHz", target_config->frequency_mhz);
    pthread_mutex_unlock(&mc3_work_lock);
    return true;
}

bool MC3_start_qualification_work(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    float frequency_mhz = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    pthread_mutex_lock(&mc3_work_lock);
    bool started = mc3_program_qualification_work(frequency_mhz);
    pthread_mutex_unlock(&mc3_work_lock);

    if (started) {
        ESP_LOGI(TAG, "Started broadcast qualification work at %.0f MHz", frequency_mhz);
    } else {
        ESP_LOGE(TAG, "Failed to start broadcast qualification work at %.0f MHz", frequency_mhz);
    }
    return started;
}

bool MC3_qualify_frequency(void *pvParameters, float frequency,
    mc3_qualification_result_t *result_out)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    mc3_qualification_result_t result = {0};
    bool success = false;

    if (result_out == NULL || mc3_chip_count == 0 ||
        mc3_chip_count > MC3_QUALIFICATION_MAX_CHIPS) {
        return false;
    }

    pthread_mutex_lock(&mc3_work_lock);

    // Stop the current search before touching the PLL. Re-applying mining
    // configuration below resets every core and starts identical broadcast
    // work on the whole chain at the new frequency.
    if (!mc3_write_register(0, MC3_WORK_CFG,
            MC3_DEFAULT_WORK_CFG_APPLY_AND_RESET, MC3_CHIP_NUM_ALL)) {
        ESP_LOGE(TAG, "Failed to quiesce MC3 cores before %.0f MHz qualification", frequency);
        goto done;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    result.frequency_mhz = MC3_send_hash_frequency(frequency);
    if (result.frequency_mhz <= 0.0f) {
        goto done;
    }
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = result.frequency_mhz;

    if (!mc3_program_qualification_work(result.frequency_mhz)) {
        goto done;
    }

    mc3_start_spdlog_window(0, MC3_CHIP_NUM_ALL,
        MC3_QUALIFICATION_SPDLOG_TIMER_COUNT);
    uint32_t measurement_ms = (uint32_t)(
        mc3_spdlog_runtime_seconds(MC3_QUALIFICATION_SPDLOG_TIMER_COUNT) * 1000.0) +
        MC3_QUALIFICATION_SETTLE_TIME_MS;
    ESP_LOGI(TAG, "Qualifying %.0f MHz for %.3f seconds on %u chips",
        result.frequency_mhz, (double)measurement_ms / 1000.0, mc3_chip_count);
    vTaskDelay(pdMS_TO_TICKS(measurement_ms));

    result.chip_count = mc3_chip_count;
    for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
        double hashrate_ghs = 0.0;
        if (!mc3_read_spdlog_chip(GLOBAL_STATE, chip_id,
                &result.passed[chip_id], &result.failed[chip_id], &hashrate_ghs)) {
            goto done;
        }
        result.hashrate_ghs[chip_id] = (float)hashrate_ghs;
        result.total_hashrate_ghs += result.hashrate_ghs[chip_id];
    }

    success = true;

done:
    *result_out = result;
    pthread_mutex_unlock(&mc3_work_lock);
    return success;
}

task_result * MC3_process_work(void * pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    memset(&result, 0, sizeof(result));
    if (mc3_pending_pop(&result)) {
        return &result;
    }

    uint8_t chip_count = mc3_chip_count;
    if (chip_count == 0) {
        chip_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    }
    if (chip_count == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return NULL;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - mc3_last_nonce_poll_us < MC3_NONCE_POLL_INTERVAL_US) {
        vTaskDelay(pdMS_TO_TICKS(10));
        return NULL;
    }
    mc3_last_nonce_poll_us = now_us;

    uint8_t chip_id = mc3_next_nonce_chip % chip_count;
    mc3_next_nonce_chip = (mc3_next_nonce_chip + 1) % chip_count;
    mc3_poll_nonce_chip(GLOBAL_STATE, chip_id);

    if (mc3_pending_pop(&result)) {
        return &result;
    }

    return NULL;
}

void MC3_read_registers(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    uint8_t chip_count = mc3_chip_count;

    if (chip_count == 0) {
        return;
    }

    asic_tuning_state_t scan_state = mc3_get_core_scan_state();
    if (scan_state == ASIC_TUNING_QUEUED) {
        if (mc3_spdlog_started) {
            pthread_mutex_lock(&mc3_work_lock);
            if (mc3_get_core_scan_state() == ASIC_TUNING_QUEUED && mc3_spdlog_started) {
                mc3_begin_core_scan();
            }
            pthread_mutex_unlock(&mc3_work_lock);
        }
        return;
    }
    if (scan_state == ASIC_TUNING_READING) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t required_runtime_us = scan_state == ASIC_TUNING_MEASURING
        ? (int64_t)(mc3_spdlog_runtime_seconds(MC3_TUNING_SPDLOG_TIMER_COUNT) * 1000000.0) + MC3_TUNING_SETTLE_TIME_US
        : MC3_SPDLOG_POLL_INTERVAL_US;
    if (!mc3_spdlog_started || now_us - mc3_spdlog_start_us < required_runtime_us) {
        return;
    }

    if (scan_state == ASIC_TUNING_MEASURING) {
        pthread_mutex_lock(&mc3_work_lock);
        now_us = esp_timer_get_time();
        if (now_us - mc3_spdlog_start_us < required_runtime_us) {
            pthread_mutex_unlock(&mc3_work_lock);
            return;
        }
        mc3_collect_core_scan(GLOBAL_STATE);
        mc3_start_spdlog(0, MC3_CHIP_NUM_ALL);
        pthread_mutex_unlock(&mc3_work_lock);
        return;
    }

    pthread_mutex_lock(&mc3_work_lock);
    now_us = esp_timer_get_time();
    if (!mc3_spdlog_started || now_us - mc3_spdlog_start_us < MC3_SPDLOG_POLL_INTERVAL_US) {
        pthread_mutex_unlock(&mc3_work_lock);
        return;
    }

    char summary[512] = "SPDLOG:";
    size_t summary_length = strlen(summary);
    double total_hashrate_ghs = 0.0;

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        uint32_t passed = 0;
        uint32_t failed = 0;
        double pass_hashrate_ghs = 0.0;
        int written;

        if (mc3_read_spdlog_chip(GLOBAL_STATE, chip_id, &passed, &failed, &pass_hashrate_ghs)) {
            total_hashrate_ghs += pass_hashrate_ghs;
            written = snprintf(summary + summary_length, sizeof(summary) - summary_length,
                " chip%u=%.2f GH/s pass/fail=%" PRIu32 "/%" PRIu32,
                chip_id, pass_hashrate_ghs, passed, failed);
        } else {
            written = snprintf(summary + summary_length, sizeof(summary) - summary_length,
                " chip%u=ERR", chip_id);
        }

        if (written > 0) {
            size_t appended = (size_t)written;
            size_t remaining = sizeof(summary) - summary_length;
            summary_length += appended < remaining ? appended : remaining - 1;
        }
    }

    snprintf(summary + summary_length, sizeof(summary) - summary_length,
        " total=%.2f GH/s", total_hashrate_ghs);
    ESP_LOGI(TAG, "%s", summary);

    mc3_start_spdlog(0, MC3_CHIP_NUM_ALL);
    pthread_mutex_unlock(&mc3_work_lock);
}

void MC3_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores)
{
    (void)nonce_percent;
    (void)frequency;
    (void)asic_count;
    (void)cores;
}

uint8_t MC3_read_temperatures(float *temps, size_t max_temps)
{
    uint8_t chip_count = mc3_chip_count;

    if (temps == NULL || max_temps == 0 || chip_count == 0) {
        return 0;
    }
    if (chip_count > max_temps) {
        chip_count = max_temps;
    }

    if (!mc3_trigger_thermal_conversion()) {
        return 0;
    }

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        uint32_t raw = 0;
        if (mc3_read_register(chip_id, MC3_THERMAL_DOUT, &raw)) {
            temps[chip_id] = mc3_temperature_from_dout(raw);
        } else {
            temps[chip_id] = -1.0f;
        }
    }

    return chip_count;
}

uint8_t MC3_read_vdd_voltages(float *voltages_mv, size_t max_voltages)
{
    uint8_t chip_count = mc3_chip_count;

    if (voltages_mv == NULL || max_voltages == 0 || chip_count == 0) {
        return 0;
    }
    if (chip_count > max_voltages) {
        chip_count = max_voltages;
    }

    if (!mc3_trigger_voltage_conversion()) {
        return 0;
    }

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        uint32_t raw_a = 0;
        uint32_t raw_b = 0;
        bool read_a = mc3_read_register(chip_id, mc3_voltage_dout_register(MC3_VOLTAGE_VDD_CHANNEL_A), &raw_a);
        bool read_b = mc3_read_register(chip_id, mc3_voltage_dout_register(MC3_VOLTAGE_VDD_CHANNEL_B), &raw_b);

        if (read_a && read_b) {
            float sensor_voltage = (mc3_voltage_from_dout(raw_a) + mc3_voltage_from_dout(raw_b)) / 2.0f;
            voltages_mv[chip_id] = sensor_voltage * MC3_VOLTAGE_VDD_SCALE * 1000.0f;
        } else {
            voltages_mv[chip_id] = -1.0f;
        }
    }

    return chip_count;
}
