#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "main/global_state.h"
#include "main/bap/bap.h"
#define TEST_STUB_GLOBAL_STATE_H

#define nvs_config_get_string bap_test_get_string
#define nvs_config_set_string bap_test_set_string
#define nvs_config_set_float bap_test_set_float
#define nvs_config_set_u16 bap_test_set_u16
#define nvs_config_set_bool bap_test_set_bool
#define esp_restart bap_test_restart
#define esp_timer_get_time bap_test_time
#define get_wifi_current_rssi bap_test_rssi
#define BAP_send_message bap_test_send
#define BAP_send_message_with_queue bap_test_send
#include "nvs_config.h"
int64_t bap_test_time(void);
void bap_test_restart(void);
esp_err_t bap_test_rssi(int8_t *rssi);
void bap_test_send(bap_command_t command, const char *parameter, const char *value);
#define TAG bap_protocol_test_tag
#include "main/bap/bap_protocol.c"
#undef TAG
#define TAG bap_handlers_test_tag
#include "main/bap/bap_handlers.c"
#undef TAG
#define TAG bap_subscription_test_tag
#include "main/bap/bap_subscription.c"
#undef TAG

QueueHandle_t bap_uart_send_queue;
SemaphoreHandle_t bap_uart_send_mutex;
SemaphoreHandle_t bap_subscription_mutex;
GlobalState *bap_global_state;

static int64_t clock_us;
static unsigned sent;
static char (*responses)[BAP_MAX_MESSAGE_LEN + 1];
static unsigned settings_written;

int64_t bap_test_time(void) { return clock_us; }
char *bap_test_get_string(NvsConfigKey key) { (void)key; return strdup(""); }
void bap_test_set_string(NvsConfigKey key, const char *value) { (void)key; (void)value; ++settings_written; }
void bap_test_set_float(NvsConfigKey key, float value) { (void)key; (void)value; ++settings_written; }
void bap_test_set_u16(NvsConfigKey key, uint16_t value) { (void)key; (void)value; ++settings_written; }
void bap_test_set_bool(NvsConfigKey key, bool value) { (void)key; (void)value; ++settings_written; }
void bap_test_restart(void) { TEST_FAIL_MESSAGE("Unexpected restart"); }
esp_err_t bap_test_rssi(int8_t *rssi) { *rssi = -42; return ESP_OK; }
void bap_test_send(bap_command_t command, const char *parameter, const char *value)
{
    TEST_ASSERT_LESS_THAN(12, sent);
    TEST_ASSERT_NOT_EQUAL(0, BAP_format_message(responses[sent++], sizeof(responses[0]),
                                               BAP_command_to_string(command), parameter, value));
}

static void reset_fixture(GlobalState *state)
{
    memset(state, 0, sizeof(*state));
    state->DEVICE_CONFIG.family.name = "Bonanza";
    state->DEVICE_CONFIG.family.asic.name = "BZM";
    state->SYSTEM_MODULE.is_connected = true;
    state->SYSTEM_MODULE.current_hashrate = 1200.55f;
    strcpy(state->SYSTEM_MODULE.best_diff_string, "123T");
    strcpy(state->SYSTEM_MODULE.ip_addr_str, "192.168.1.234");
    state->SYSTEM_MODULE.pools[0].url = "pool.example";
    state->SYSTEM_MODULE.pools[0].user = "miner";
    state->SYSTEM_MODULE.pools[0].port = 3333;
    state->SYSTEM_MODULE.ssid = "test";
    state->POWER_MANAGEMENT_MODULE.chip_temp_avg = 58.25f;
    state->POWER_MANAGEMENT_MODULE.power = 17.6f;
    state->POWER_MANAGEMENT_MODULE.fan_rpm = 4200;
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 800;
    state->POWER_MANAGEMENT_MODULE.frequency_value = 1350;
    bap_global_state = state;
    responses = calloc(12, sizeof(*responses));
    TEST_ASSERT_NOT_NULL(responses);
    if (!bap_subscription_mutex) bap_subscription_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(bap_subscription_mutex);
    clock_us = 10000000;
    sent = settings_written = 0;
    BAP_handlers_init(state);
    BAP_subscription_init();
}

static void command(const char *cmd, const char *parameter, const char *value)
{
    char message[BAP_MAX_MESSAGE_LEN + 1];
    TEST_ASSERT_NOT_EQUAL(0, BAP_format_message(message, sizeof(message), cmd, parameter, value));
    BAP_parse_message(message);
}

static void expect_response(unsigned index, const char *body)
{
    char decoded[BAP_MAX_MESSAGE_LEN];
    TEST_ASSERT_LESS_THAN(sent, index);
    TEST_ASSERT_TRUE(BAP_decode_sentence(responses[index], decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_STRING(body, decoded);
}

TEST_CASE("BAP formats known frames and rejects truncated or injected values", "[asic][bap]")
{
    char frame[BAP_MAX_MESSAGE_LEN + 1], body[BAP_MAX_MESSAGE_LEN];
    TEST_ASSERT_EQUAL(24, BAP_format_message(frame, sizeof(frame), "REQ", "systemInfo", NULL));
    TEST_ASSERT_EQUAL_STRING("$BAP,REQ,systemInfo*3E\r\n", frame);
    TEST_ASSERT_TRUE(BAP_decode_sentence(frame, body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("BAP,REQ,systemInfo", body);
    TEST_ASSERT_EQUAL(0, BAP_format_message(frame, 10, "REQ", "systemInfo", NULL));
    const char *bad[] = {"x\r\n$BAP", "x*00", "$bad", "non-ascii\x80"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        TEST_ASSERT_EQUAL(0, BAP_format_message(frame, sizeof(frame), "RES", "name", bad[i]));
    char value[BAP_MAX_MESSAGE_LEN];
    memset(value, 'a', sizeof(value));
    value[sizeof(value) - 1] = 0;
    TEST_ASSERT_EQUAL(0, BAP_format_message(frame, sizeof(frame), "RES", "name", value));
    value[237] = 0; // 256 wire bytes including CRLF, plus storage for NUL.
    TEST_ASSERT_EQUAL(256, BAP_format_message(frame, sizeof(frame), "RES", "name", value));
    TEST_ASSERT_TRUE(BAP_decode_sentence(frame, body, sizeof(body)));
}

TEST_CASE("BAP rejects bad checksums and malformed subscriptions before dispatch", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    const char *bad[] = {"$BAP,SUB,hashrate,1000*00\r\n", "$BAP,SUB,hashrate,1000\r\n",
                         "$BAP,REQ,systemInfo*ZZ\n", "$BAP,REQ,systemInfo*3Ex\n",
                         "$BAP,REQ,systemInfo*3E\rX\n", "$BAP,,REQ,systemInfo*3E\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) BAP_parse_message(bad[i]);
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(0, sent);
    command("SUB", "hashrate", "1000oops");
    expect_response(0, "BAP,ERR,hashrate,invalid_request");
    command("SUB", "unknownGroup", "1000");
    expect_response(1, "BAP,ERR,unknownGroup,unknown_parameter");
    command("REQ", "systemInfo", "extra");
    expect_response(2, "BAP,ERR,systemInfo,invalid_request");
    command("REQ", "missingGroup", NULL);
    expect_response(3, "BAP,ERR,missingGroup,unknown_parameter");
    TEST_ASSERT_FALSE(subscriptions[BAP_PARAM_HASHRATE].active);
    TEST_ASSERT_EQUAL(0, settings_written);
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

TEST_CASE("BAP stream handles fragmentation CRLF noise overruns and resynchronization", "[asic][bap]")
{
    bap_stream_t stream = {0};
    const char *parts[] = {"noise$BAP,RE", "Q,systemInfo*3e\r", "\n"};
    unsigned frames = 0;
    char body[BAP_MAX_MESSAGE_LEN];
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        for (const char *p = parts[i]; *p; ++p) {
            if (BAP_stream_feed(&stream, *p)) {
                ++frames;
                TEST_ASSERT_TRUE(BAP_decode_sentence(stream.message, body, sizeof(body)));
            }
        }
    }
    TEST_ASSERT_EQUAL(1, frames);
    TEST_ASSERT_EQUAL_STRING("BAP,REQ,systemInfo", body);
    BAP_stream_feed(&stream, '$');
    for (unsigned i = 0; i < 600; ++i) TEST_ASSERT_FALSE(BAP_stream_feed(&stream, 'x'));
    TEST_ASSERT_FALSE(BAP_stream_feed(&stream, '\n'));
    const char *recover = "$partial$BAP,REQ,systemInfo*3E\n";
    for (const char *p = recover; *p; ++p) {
        if (BAP_stream_feed(&stream, *p)) ++frames;
    }
    TEST_ASSERT_EQUAL(2, frames);
    TEST_ASSERT_TRUE(BAP_decode_sentence(stream.message, body, sizeof(body)));
    BAP_stream_feed(&stream, '$');
    BAP_stream_feed(&stream, 0);
    TEST_ASSERT_FALSE(BAP_stream_feed(&stream, '\n'));
}

TEST_CASE("BAP serves Bonanza identity and live telemetry with display units", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    command("REQ", "systemInfo", NULL);
    expect_response(0, "BAP,RES,deviceModel,Bonanza");
    expect_response(1, "BAP,RES,asicModel,BZM");
    expect_response(5, "BAP,RES,frequency,800.00");
    sent = 0;
    command("REQ", "hashrate", NULL);
    command("REQ", "temperature", NULL);
    command("REQ", "power", NULL);
    command("REQ", "fan_speed", NULL);
    command("REQ", "best_difficulty", NULL);
    command("REQ", "wifi", NULL);
    expect_response(0, "BAP,RES,hashrate,1200.55");
    expect_response(1, "BAP,RES,chipTemp,58.250000");
    expect_response(3, "BAP,RES,power,17.60");
    expect_response(4, "BAP,RES,fan_speed,4200");
    expect_response(5, "BAP,RES,best_difficulty,123T");
    expect_response(6, "BAP,RES,wifi_ip,192.168.1.234");
    state->SYSTEM_MODULE.current_hashrate = 0;
    command("REQ", "hashrate", NULL);
    expect_response(7, "BAP,RES,hashrate,0.00");
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

TEST_CASE("BAP renews display subscriptions with fresh snapshots and expires them", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    command("SUB", "fan_speed", "1000");
    expect_response(0, "BAP,ACK,fan_speed,subscribed");
    BAP_send_subscription_update(state);
    expect_response(1, "BAP,RES,fan_speed,4200");
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(2, sent); // Stable values are suppressed.
    command("SUB", "fan_speed", "1000");
    BAP_send_subscription_update(state);
    expect_response(3, "BAP,RES,fan_speed,4200");
    state->POWER_MANAGEMENT_MODULE.fan_rpm = 0;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect_response(4, "BAP,RES,fan_speed,0");
    clock_us += 300000001;
    BAP_send_subscription_update(state);
    expect_response(5, "BAP,STA,fan_speed,subscription_timeout");
    TEST_ASSERT_FALSE(subscriptions[BAP_PARAM_FAN_SPEED].active);
    command("SUB", "fan_speed", "1000");
    BAP_send_subscription_update(state);
    expect_response(7, "BAP,RES,fan_speed,0");
    command("UNSUB", "fan_speed", NULL);
    expect_response(8, "BAP,ACK,fan_speed,unsubscribed");
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(9, sent);
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

TEST_CASE("BAP subscription intervals are bounded decimal milliseconds", "[asic][bap]")
{
    uint32_t interval = 0;
    TEST_ASSERT_TRUE(BAP_parse_interval("100", &interval));
    TEST_ASSERT_EQUAL(100, interval);
    TEST_ASSERT_TRUE(BAP_parse_interval("60000", &interval));
    TEST_ASSERT_EQUAL(60000, interval);
    const char *bad[] = {"", "0", "99", "60001", "-1000", "+1000", " 1000", "1000 ",
                         "1e3", "1000,extra", "9999999999999999999999"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        TEST_ASSERT_FALSE(BAP_parse_interval(bad[i], &interval));
}

TEST_CASE("BAP retains AP mode telemetry gating", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    state->SYSTEM_MODULE.is_connected = false;
    command("REQ", "systemInfo", NULL);
    command("SUB", "hashrate", "1000");
    expect_response(0, "BAP,ERR,systemInfo,ap_mode_no_requests");
    expect_response(1, "BAP,ERR,hashrate,ap_mode_no_subscriptions");
    TEST_ASSERT_FALSE(subscriptions[BAP_PARAM_HASHRATE].active);
    state->SYSTEM_MODULE.is_connected = true;
    command("REQ", "systemInfo", NULL);
    expect_response(2, "BAP,RES,deviceModel,Bonanza");
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

TEST_CASE("BAP telemetry subscriptions use live values and republish on display reconnect", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    const char *groups[] = {"hashrate", "temperature", "power", "fan_speed", "best_difficulty", "wifi"};
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) command("SUB", groups[i], "1000");
    sent = 0;
    BAP_send_subscription_update(state);
    expect_response(0, "BAP,RES,hashrate,1200.55");
    expect_response(1, "BAP,RES,chipTemp,58.250000");
    expect_response(3, "BAP,RES,power,17.60");
    expect_response(4, "BAP,RES,fan_speed,4200");
    expect_response(5, "BAP,RES,best_difficulty,123T");
    expect_response(9, "BAP,RES,wifi_ip,192.168.1.234");
    TEST_ASSERT_EQUAL(10, sent);
    sent = 0;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(0, sent);
    state->SYSTEM_MODULE.current_hashrate = 0;
    state->POWER_MANAGEMENT_MODULE.power = 0;
    state->POWER_MANAGEMENT_MODULE.chip_temp_avg = 55;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect_response(0, "BAP,RES,hashrate,0.00");
    expect_response(1, "BAP,RES,chipTemp,55.000000");
    expect_response(2, "BAP,RES,power,0.00");
    sent = 0;
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) command("SUB", groups[i], "1000");
    sent = 0;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(10, sent);
    expect_response(0, "BAP,RES,hashrate,0.00");
    expect_response(9, "BAP,RES,wifi_ip,192.168.1.234");
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}

TEST_CASE("BAP publishes actual frequency on display probes requests and subscriptions", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    reset_fixture(state);
    command("REQ", "systemInfo", NULL);
    expect_response(5, "BAP,RES,frequency,800.00");
    sent = 0;
    command("REQ", "frequency", NULL);
    expect_response(0, "BAP,RES,frequency,800.00");
    command("SUB", "frequency", "1000");
    expect_response(1, "BAP,ACK,frequency,subscribed");
    BAP_send_subscription_update(state);
    expect_response(2, "BAP,RES,frequency,800.00");
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    TEST_ASSERT_EQUAL(3, sent);
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 1000;
    clock_us += 1000000;
    BAP_send_subscription_update(state);
    expect_response(3, "BAP,RES,frequency,1000.00");
    command("REQ", "systemInfo", NULL);
    expect_response(9, "BAP,RES,frequency,1000.00");
    sent = 0;
    command("SUB", "frequency", "1000");
    BAP_send_subscription_update(state);
    expect_response(1, "BAP,RES,frequency,1000.00");
    free(responses);
    responses = NULL;
    free(state);
    bap_global_state = NULL;
    vSemaphoreDelete(bap_subscription_mutex);
    bap_subscription_mutex = NULL;
}
