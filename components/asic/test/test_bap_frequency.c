#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "main/global_state.h"
#include "main/bap/bap.h"
#define TEST_STUB_GLOBAL_STATE_H

#define nvs_config_get_string frequency_test_get_string
#define nvs_config_set_string frequency_test_set_string
#define nvs_config_set_float frequency_test_set_float
#define nvs_config_set_u16 frequency_test_set_u16
#define nvs_config_set_bool frequency_test_set_bool
#define esp_restart frequency_test_restart
#define esp_timer_get_time frequency_test_time
#define get_wifi_current_rssi frequency_test_rssi
#define BAP_send_message frequency_test_send
#define BAP_send_message_with_queue frequency_test_send
#define SYSTEM_get_display_hashes frequency_test_display_hashes
#define ASIC_get_chip_details frequency_test_chip_details
#define ASIC_get_asic_measurement frequency_test_asic_rate
#define ASIC_get_domain_measurement frequency_test_domain_rate
#include "asic.h"
size_t frequency_test_chip_details(GlobalState *state, asic_chip_detail_t *details, size_t max_chips);
esp_err_t frequency_test_asic_rate(GlobalState *state, uint8_t asic_nr, asic_domain_measurement_t *measurement);
esp_err_t frequency_test_domain_rate(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr, asic_domain_measurement_t *measurement);
#include "nvs_config.h"
int64_t frequency_test_time(void);
void frequency_test_restart(void);
esp_err_t frequency_test_rssi(int8_t *rssi);
void frequency_test_send(bap_command_t command, const char *parameter, const char *value);
void frequency_test_display_hashes(GlobalState *state, char network_target[65], char best_share[65], char last_share[65]);
#define TAG frequency_protocol_tag
#include "main/bap/bap_protocol.c"
#undef TAG
#define TAG frequency_handlers_tag
#include "main/bap/bap_handlers.c"
#undef TAG
#define TAG frequency_subscription_tag
#include "main/bap/bap_subscription.c"
#undef TAG

QueueHandle_t bap_uart_send_queue;
SemaphoreHandle_t bap_uart_send_mutex;
SemaphoreHandle_t bap_subscription_mutex;
GlobalState *bap_global_state;

static int64_t clock_us;
static unsigned sent;
static struct {
    bap_command_t command;
    char parameter[32];
    char value[96];
} responses[16];

int64_t frequency_test_time(void) { return clock_us; }
static asic_chip_detail_t test_details[4];
static size_t test_detail_count;
static float test_asic_rates[4];
static bool test_rates_available;
static float test_domain_rates[4][4];

size_t frequency_test_chip_details(GlobalState *state, asic_chip_detail_t *details, size_t max_chips)
{
    (void)state;
    size_t count = test_detail_count < max_chips ? test_detail_count : max_chips;
    memcpy(details, test_details, count * sizeof(*details));
    return count;
}
esp_err_t frequency_test_asic_rate(GlobalState *state, uint8_t asic_nr, asic_domain_measurement_t *measurement)
{
    (void)state;
    measurement->hashrate = test_asic_rates[asic_nr];
    return test_rates_available ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t frequency_test_domain_rate(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr, asic_domain_measurement_t *measurement)
{
    (void)state;
    measurement->hashrate = test_domain_rates[asic_nr][domain_nr];
    return test_rates_available ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static const char *test_hostname = "";
static const char *test_network_target = "";
static const char *test_best_share = "";
static const char *test_last_share = "";

char *frequency_test_get_string(NvsConfigKey key)
{
    return strdup(key == NVS_CONFIG_HOSTNAME ? test_hostname : "");
}
void frequency_test_display_hashes(GlobalState *state, char network_target[65], char best_share[65], char last_share[65])
{
    (void)state;
    snprintf(network_target, 65, "%s", test_network_target);
    snprintf(best_share, 65, "%s", test_best_share);
    snprintf(last_share, 65, "%s", test_last_share);
}
void frequency_test_set_string(NvsConfigKey key, const char *value) { (void)key; (void)value; }
void frequency_test_set_float(NvsConfigKey key, float value) { (void)key; (void)value; }
void frequency_test_set_u16(NvsConfigKey key, uint16_t value) { (void)key; (void)value; }
void frequency_test_set_bool(NvsConfigKey key, bool value) { (void)key; (void)value; }
void frequency_test_restart(void) { TEST_FAIL_MESSAGE("Unexpected restart"); abort(); }
esp_err_t frequency_test_rssi(int8_t *rssi) { *rssi = -42; return ESP_OK; }
void frequency_test_send(bap_command_t command, const char *parameter, const char *value)
{
    TEST_ASSERT_LESS_THAN(16, sent);
    responses[sent].command = command;
    snprintf(responses[sent].parameter, sizeof(responses[sent].parameter), "%s", parameter);
    snprintf(responses[sent].value, sizeof(responses[sent].value), "%s", value ? value : "");
    ++sent;
}

static GlobalState *fixture(void)
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    state->DEVICE_CONFIG.family.name = "Bonanza";
    state->DEVICE_CONFIG.family.asic.name = "BZM";
    state->SYSTEM_MODULE.is_connected = true;
    state->SYSTEM_MODULE.current_hashrate = 1200.55f;
    state->SYSTEM_MODULE.pools[0].url = "pool.example";
    state->SYSTEM_MODULE.pools[0].user = "miner";
    state->SYSTEM_MODULE.pools[0].port = 3333;
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 800;
    state->POWER_MANAGEMENT_MODULE.frequency_value = 1350;
    bap_global_state = state;
    bap_subscription_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(bap_subscription_mutex);
    clock_us += 10000000;
    sent = 0;
    TEST_ASSERT_EQUAL(ESP_OK, BAP_handlers_init(state));
    TEST_ASSERT_EQUAL(ESP_OK, BAP_subscription_init());
    return state;
}

static void release_fixture(GlobalState *state)
{
    test_hostname = test_network_target = test_best_share = test_last_share = "";
    test_detail_count = 0;
    test_rates_available = false;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

static void command(const char *body, int checksum)
{
    char message[BAP_MAX_MESSAGE_LEN];
    if (checksum) {
        uint8_t value = BAP_calculate_checksum(body) ^ (checksum < 0 ? 1 : 0);
        snprintf(message, sizeof(message), "$%s*%02X\r\n", body, value);
    } else {
        snprintf(message, sizeof(message), "$%s\r\n", body);
    }
    BAP_parse_message(message);
}

static void expect(unsigned index, bap_command_t cmd, const char *parameter, const char *value)
{
    TEST_ASSERT_TRUE(index < sent);
    TEST_ASSERT_EQUAL(cmd, responses[index].command);
    TEST_ASSERT_EQUAL_STRING(parameter, responses[index].parameter);
    TEST_ASSERT_EQUAL_STRING(value, responses[index].value);
}

TEST_CASE("BAP frequency subscription pushes actual MHz changes and stops on unsubscribe", "[asic][bap][frequency]")
{
    GlobalState *state = fixture();
    command("BAP,SUB,frequency,1000", 1);
    expect(0, BAP_CMD_ACK, "frequency", "subscribed");
    BAP_send_subscription_update(state);
    expect(1, BAP_CMD_RES, "frequency", "800.00");
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 1000;
    clock_us += 999000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(2, sent);
    clock_us += 1000;
    BAP_send_subscription_update(state);
    expect(2, BAP_CMD_RES, "frequency", "1000.00");
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(3, sent);
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 0;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(3, BAP_CMD_RES, "frequency", "0.00");
    command("BAP,UNSUB,frequency", 1);
    expect(4, BAP_CMD_ACK, "frequency", "unsubscribed");
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 1200;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(5, sent);
    release_fixture(state);
}

TEST_CASE("BAP frequency subscriptions renew snapshots and retain the five-minute expiry", "[asic][bap][frequency]")
{
    GlobalState *state = fixture();
    command("BAP,SUB,frequency,1000", 1);
    BAP_send_subscription_update(state);
    expect(1, BAP_CMD_RES, "frequency", "800.00");
    clock_us += 60000000;
    command("BAP,SUB,frequency,1000", 1);
    expect(2, BAP_CMD_ACK, "frequency", "subscribed");
    BAP_send_subscription_update(state);
    expect(3, BAP_CMD_RES, "frequency", "800.00");
    clock_us += 300001000;
    BAP_send_subscription_update(state);
    expect(4, BAP_CMD_STA, "frequency", "subscription_timeout");
    TEST_ASSERT_EQUAL(5, sent);
    command("BAP,SUB,frequency,1000", 1);
    BAP_send_subscription_update(state);
    expect(6, BAP_CMD_RES, "frequency", "800.00");
    release_fixture(state);
}

TEST_CASE("BAP frequency retains AP gating and legacy subscription input handling", "[asic][bap][frequency]")
{
    GlobalState *state = fixture();
    state->SYSTEM_MODULE.is_connected = false;
    command("BAP,SUB,frequency,1000", 1);
    expect(0, BAP_CMD_ERR, "frequency", "ap_mode_no_subscriptions");
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(1, sent);
    release_fixture(state);
    const struct {
        const char *body;
        int checksum;
        uint32_t interval_ms;
    } cases[] = {
        {"BAP,SUB,frequency", 1, 3000},
        {"BAP,SUB,frequency,50", 0, 50},
        {"BAP,SUB,frequency,1000oops", -1, 1000},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        state = fixture();
        command(cases[i].body, cases[i].checksum);
        expect(0, BAP_CMD_ACK, "frequency", "subscribed");
        BAP_send_subscription_update(state);
        expect(1, BAP_CMD_RES, "frequency", "800.00");
        state->POWER_MANAGEMENT_MODULE.actual_frequency = 1000;
        clock_us += (cases[i].interval_ms - 1) * 1000;
        BAP_send_subscription_update(state);
        TEST_ASSERT_EQUAL(2, sent);
        clock_us += 1000;
        BAP_send_subscription_update(state);
        expect(2, BAP_CMD_RES, "frequency", "1000.00");
        release_fixture(state);
    }
}

TEST_CASE("BAP identity appends frequency after existing replies and preserves subscriptions", "[asic][bap][frequency]")
{
    GlobalState *state = fixture();
    command("BAP,REQ,systemInfo", 1);
    expect(0, BAP_CMD_RES, "deviceModel", "Bonanza");
    expect(1, BAP_CMD_RES, "asicModel", "BZM");
    expect(2, BAP_CMD_RES, "pool", "pool.example");
    expect(3, BAP_CMD_RES, "poolPort", "3333");
    expect(4, BAP_CMD_RES, "poolUser", "miner");
    expect(5, BAP_CMD_RES, "frequency", "800.00");
    TEST_ASSERT_EQUAL(6, sent);
    sent = 0;
    command("BAP,SUB,hashrate,1000", 1);
    command("BAP,SUB,frequency,1000", 1);
    BAP_send_subscription_update(state);
    expect(2, BAP_CMD_RES, "hashrate", "1200.55");
    expect(3, BAP_CMD_RES, "frequency", "800.00");
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 1000;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(4, BAP_CMD_RES, "frequency", "1000.00");
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(5, sent);
    command("BAP,UNSUB,frequency", 1);
    expect(5, BAP_CMD_ACK, "frequency", "unsubscribed");
    state->SYSTEM_MODULE.current_hashrate = 2222;
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 1200;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(6, BAP_CMD_RES, "hashrate", "2222.00");
    TEST_ASSERT_EQUAL(7, sent);
    release_fixture(state);
}

TEST_CASE("BAP one-shot telemetry reads return current values without creating subscriptions", "[asic][bap][requests]")
{
    GlobalState *state = fixture();
    state->POWER_MANAGEMENT_MODULE.chip_temp_avg = 58.25f;
    state->POWER_MANAGEMENT_MODULE.vr_temp = 40.5f;
    state->POWER_MANAGEMENT_MODULE.power = 17.6f;
    state->POWER_MANAGEMENT_MODULE.fan_rpm = 4200;
    strcpy(state->SYSTEM_MODULE.best_diff_string, "123T");
    strcpy(state->SYSTEM_MODULE.ip_addr_str, "192.168.1.234");
    const char *requests[] = {"frequency", "hashrate", "temperature", "power",
                              "fan_speed", "best_difficulty", "wifi"};
    for (unsigned i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        char body[64];
        snprintf(body, sizeof(body), "BAP,REQ,%s", requests[i]);
        command(body, 1);
    }
    expect(0, BAP_CMD_RES, "frequency", "800.00");
    expect(1, BAP_CMD_RES, "hashrate", "1200.55");
    expect(2, BAP_CMD_RES, "chipTemp", "58.250000");
    expect(3, BAP_CMD_RES, "vrTemp", "40.500000");
    expect(4, BAP_CMD_RES, "power", "17.60");
    expect(5, BAP_CMD_RES, "fan_speed", "4200");
    expect(6, BAP_CMD_RES, "best_difficulty", "123T");
    expect(7, BAP_CMD_RES, "wifi_ip", "192.168.1.234");
    TEST_ASSERT_EQUAL(8, sent);
    state->SYSTEM_MODULE.current_hashrate = 0;
    state->POWER_MANAGEMENT_MODULE.power = 0;
    command("BAP,REQ,hashrate", 1);
    expect(8, BAP_CMD_RES, "hashrate", "0.00");
    command("BAP,REQ,power", 1);
    expect(9, BAP_CMD_RES, "power", "0.00");
    clock_us += 5000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(10, sent);
    release_fixture(state);
}

TEST_CASE("BAP telemetry reads retain AP gating and legacy request-field handling", "[asic][bap][requests]")
{
    GlobalState *state = fixture();
    state->SYSTEM_MODULE.is_connected = false;
    command("BAP,REQ,frequency", 1);
    expect(0, BAP_CMD_ERR, "frequency", "ap_mode_no_requests");
    state->SYSTEM_MODULE.is_connected = true;
    clock_us += 1100000;
    command("BAP,REQ,frequency", 1);
    expect(1, BAP_CMD_RES, "frequency", "800.00");
    clock_us += 100000;
    command("BAP,REQ,frequency", 1);
    TEST_ASSERT_EQUAL(2, sent);
    command("BAP,REQ,shares,legacy", 1);
    expect(2, BAP_CMD_RES, "shares", "0/0");
    command("BAP,REQ,hashrate,legacy", 1);
    expect(3, BAP_CMD_RES, "hashrate", "1200.55");
    command("BAP,REQ,futureMetric", 1);
    TEST_ASSERT_EQUAL(4, sent);
    release_fixture(state);
}

#define NET_TARGET "00000000000000000002a1b10000000000000000000000000000000000000000"
#define BEST_HASH  "000000000000000140e5b3ae1c5a3d6e7f8091a2b3c4d5e6f708192a3b4c5d6e"
#define LAST_HASH  "0000000000003f1e2d3c4b5a69788796a5b4c3d2e1f00112233445566778899a"

TEST_CASE("BAP identity appends display fields once the host has them", "[asic][bap][display]")
{
    GlobalState *state = fixture();
    snprintf(state->network_diff_string, sizeof(state->network_diff_string), "%s", "129.44T");
    test_hostname = "bitaxeBonanza-1004x";
    test_network_target = NET_TARGET;
    test_best_share = BEST_HASH;
    test_last_share = LAST_HASH;
    command("BAP,REQ,systemInfo", 1);
    expect(4, BAP_CMD_RES, "poolUser", "miner");
    expect(5, BAP_CMD_RES, "frequency", "800.00");
    expect(6, BAP_CMD_RES, "networkDifficulty", "129.44T");
    expect(7, BAP_CMD_RES, "hostname", "bitaxeBonanza-1004x");
    expect(8, BAP_CMD_RES, "networkTarget", NET_TARGET);
    expect(9, BAP_CMD_RES, "bestShareHash", BEST_HASH);
    expect(10, BAP_CMD_RES, "lastShareHash", LAST_HASH);
    TEST_ASSERT_EQUAL(11, sent);
    release_fixture(state);
}

TEST_CASE("BAP pushes share hashes with best_difficulty and the target with block_height", "[asic][bap][display]")
{
    GlobalState *state = fixture();
    command("BAP,SUB,best_difficulty,1000", 1);
    command("BAP,SUB,block_height,1000", 1);
    TEST_ASSERT_EQUAL(2, sent);
    BAP_send_subscription_update(state);
    // Nothing known yet: only the existing fields are sent.
    expect(2, BAP_CMD_RES, "best_difficulty", "");
    expect(3, BAP_CMD_RES, "block_height", "0");
    TEST_ASSERT_EQUAL(4, sent);

    test_network_target = NET_TARGET;
    test_best_share = BEST_HASH;
    test_last_share = LAST_HASH;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(4, BAP_CMD_RES, "bestShareHash", BEST_HASH);
    expect(5, BAP_CMD_RES, "lastShareHash", LAST_HASH);
    expect(6, BAP_CMD_RES, "networkTarget", NET_TARGET);
    TEST_ASSERT_EQUAL(7, sent);

    // Unchanged values are not repeated; a new last share is.
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(7, sent);
    test_last_share = BEST_HASH;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(7, BAP_CMD_RES, "lastShareHash", BEST_HASH);
    TEST_ASSERT_EQUAL(8, sent);

    // Renewing the subscription resends the current values.
    command("BAP,SUB,best_difficulty,1000", 1);
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(9, BAP_CMD_RES, "best_difficulty", "");
    expect(10, BAP_CMD_RES, "bestShareHash", BEST_HASH);
    expect(11, BAP_CMD_RES, "lastShareHash", BEST_HASH);
    TEST_ASSERT_EQUAL(12, sent);
    release_fixture(state);
}

static void four_bzm_chips(GlobalState *state)
{
    state->DEVICE_CONFIG.family.asic_count = 4;
    state->DEVICE_CONFIG.family.asic.hash_domains = 1;
    memset(test_details, 0, sizeof(test_details));
    for (int chip = 0; chip < 4; ++chip) {
        asic_chip_detail_t *detail = &test_details[chip];
        detail->domain_count = 2;
        detail->has_temperature = detail->has_voltage = detail->has_frequency = true;
        detail->temperature_c = 57.6f + chip;
        detail->domain_voltage_mv[0] = 350.4f + chip;
        detail->domain_voltage_mv[1] = 349.6f;
        detail->domain_frequency_mhz[0] = 800;
        detail->domain_frequency_mhz[1] = chip == 2 ? 787 : 800;
        test_asic_rates[chip] = 750.2f + chip;
    }
    test_detail_count = 4;
    test_rates_available = true;
}

TEST_CASE("BAP temperature subscription pushes per-chip lists for the ASIC page", "[asic][bap][display]")
{
    GlobalState *state = fixture();
    four_bzm_chips(state);
    command("BAP,SUB,temperature,1000", 1);
    BAP_send_subscription_update(state);
    expect(1, BAP_CMD_RES, "chipTemp", "0.000000");
    expect(2, BAP_CMD_RES, "vrTemp", "0.000000");
    expect(3, BAP_CMD_RES, "asicTemp", "58,59,60,61");
    expect(4, BAP_CMD_RES, "asicVoltage", "700,701,702,703");
    expect(5, BAP_CMD_RES, "domainVoltage", "350/350,351/350,352/350,353/350");
    expect(6, BAP_CMD_RES, "asicFrequency", "800,800,794,800");
    expect(7, BAP_CMD_RES, "domainFrequency", "800/800,800/800,800/787,800/800");
    expect(8, BAP_CMD_RES, "asicHashrate", "750,751,752,753");
    TEST_ASSERT_EQUAL(9, sent);   // BZM counts hashrate per chip: no domainHashrate

    // Unchanged lists are not repeated; a changed one is.
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(9, sent);
    test_asic_rates[1] = 760;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(9, BAP_CMD_RES, "asicHashrate", "750,760,752,753");
    TEST_ASSERT_EQUAL(10, sent);

    // A chip without a reading holds back that list only.
    test_details[3].has_voltage = false;
    test_details[0].temperature_c = 70;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(10, BAP_CMD_RES, "asicTemp", "70,59,60,61");
    TEST_ASSERT_EQUAL(11, sent);

    // Renewing the subscription resends every list it can.
    sent = 0;
    command("BAP,SUB,temperature,1000", 1);
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(3, BAP_CMD_RES, "asicTemp", "70,59,60,61");
    expect(4, BAP_CMD_RES, "asicFrequency", "800,800,794,800");
    expect(5, BAP_CMD_RES, "domainFrequency", "800/800,800/800,800/787,800/800");
    expect(6, BAP_CMD_RES, "asicHashrate", "750,760,752,753");
    TEST_ASSERT_EQUAL(7, sent);
    release_fixture(state);
}

TEST_CASE("BAP per-chip lists are left out when the board cannot fill them", "[asic][bap][display]")
{
    GlobalState *state = fixture();
    four_bzm_chips(state);
    test_rates_available = false;
    test_detail_count = 0;
    command("BAP,SUB,temperature,1000", 1);
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(3, sent);   // ACK, chipTemp, vrTemp

    // More chips than the display shows: nothing per chip.
    state->DEVICE_CONFIG.family.asic_count = 6;
    test_rates_available = true;
    clock_us += 1000000;   // Past the one-second duplicate filter
    command("BAP,SUB,temperature,1000", 1);
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(6, sent);   // ACK, chipTemp, vrTemp again

    // Hash-counting domains go out when no rail domains could be confused with them.
    state->DEVICE_CONFIG.family.asic_count = 2;
    state->DEVICE_CONFIG.family.asic.hash_domains = 4;
    test_domain_rates[0][0] = 300; test_domain_rates[0][1] = 301; test_domain_rates[0][2] = 302; test_domain_rates[0][3] = 303;
    test_domain_rates[1][0] = 310; test_domain_rates[1][1] = 311; test_domain_rates[1][2] = 312; test_domain_rates[1][3] = 313;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect(6, BAP_CMD_RES, "asicHashrate", "750,751");
    expect(7, BAP_CMD_RES, "domainHashrate", "300/301/302/303,310/311/312/313");
    TEST_ASSERT_EQUAL(8, sent);
    release_fixture(state);
}
