#include "unity.h"
#include "bm13xx_test_bindings.h"
#include "bm13xx_test_harness.h"
#include "bm1370.h"
#include "global_state.h"
#include <math.h>

static GlobalState *sensor_state;
static const uint64_t sample_time = 123456789;
static uint32_t control;
static unsigned sample_counts[2];
static bool data_ready, missing_chip, stop_on_start;

static uint32_t value_of(const uint8_t *bytes)
{
    return ((uint32_t)bytes[6] << 24) | ((uint32_t)bytes[7] << 16) | ((uint32_t)bytes[8] << 8) | bytes[9];
}

static void sensor_reply_hook(const uint8_t *bytes, size_t length)
{
    if (length == 11 && bytes[5] == 0xb8) {
        uint32_t value = value_of(bytes);
        if (!(value & (1U << 21))) {
            control = value;
            sample_counts[0] = sample_counts[1] = 0;
        } else if (stop_on_start) {
            sensor_state->ASIC_initalized = false;
        }
    }
    if (length != 7 || bytes[5] != 0xbd) return;
    uint8_t chip = bytes[4] / 128;
    TEST_ASSERT_LESS_THAN_UINT8(2, chip);
    if (chip && missing_chip) return;
    unsigned sample = sample_counts[chip]++;
    uint32_t raw;
    if (control & (1U << 23)) {
        uint32_t a = chip ? 28000 : 26000;
        uint32_t b = chip ? 8000 : 7361;
        if (!chip && sample == 20) { a += 19; b += 19; }
        raw = sample ? a | (b << 16) : 0;
    } else {
        uint8_t selector = (control >> 13) & 7;
        raw = sample ? (selector < 3 ? 24576 : 20480) : 16384;
        raw |= 0x45670000;
    }
    if (data_ready && sample) raw |= 0x80000000;
    const uint8_t response[11] = {0xaa, 0x55, raw >> 24, raw >> 16, raw >> 8, raw, bytes[4], 0xbd, 0, 0, 0};
    bm13xx_harness_queue_response(response);
    TEST_ASSERT_NULL(BM1370_process_work(sensor_state));
}

static void begin_sensor_test(void)
{
    sensor_state = bm13xx_harness_begin(&bm13xx_harness_drivers[2]);
    TEST_ASSERT_EQUAL_UINT8(2, BM1370_init(sensor_state));
    sensor_state->ASIC_initalized = true;
    data_ready = true;
    missing_chip = stop_on_start = false;
    bm13xx_harness_clear_packets();
    bm13xx_harness_set_serial_hook(sensor_reply_hook);
}

TEST_CASE("BM1370 temperature uses both ADC phases with stock integer correction", "[asic][temperature]")
{
    begin_sensor_test();
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_EQUAL_UINT(175, bm13xx_harness_packet_count());
    TEST_ASSERT_EQUAL_HEX32(0x238c000d, value_of(bm13xx_harness_packet(0)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x00040010, value_of(bm13xx_harness_packet(1)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x03340e80, value_of(bm13xx_harness_packet(2)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x23ac000d, value_of(bm13xx_harness_packet(3)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x3f4141af, value_of(bm13xx_harness_packet(4)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x238ce00d, value_of(bm13xx_harness_packet(87)->bytes));
    TEST_ASSERT_EQUAL_HEX32(0x3f0141af, value_of(bm13xx_harness_packet(88)->bytes));
    TEST_ASSERT_EQUAL_UINT(21, sample_counts[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0036f, BM1370_get_die_temperature(0, sample_time));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 46.0156f, BM1370_get_die_temperature(1, sample_time));
    TEST_ASSERT_FALSE(isnan(BM1370_get_die_temperature(0, sample_time + 10000000)));
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(0, sample_time + 10000001)));
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(0, sample_time - 1)));
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(2, sample_time)));
    TEST_ASSERT_EQUAL_UINT8(2, BM1370_init(sensor_state));
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(0, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1370 voltage selects four channels without the BM1372 gain factor", "[asic][voltage]")
{
    begin_sensor_test();
    BM1370_read_sensors(sensor_state);
    static const uint32_t controls[] = {0x230c230d, 0x230c430d, 0x230c830d, 0x230ca30d};
    for (unsigned channel = 0; channel < 4; channel++) {
        bm13xx_harness_clear_packets();
        BM1370_read_sensors(sensor_state);
        TEST_ASSERT_EQUAL_UINT(34, bm13xx_harness_packet_count());
        TEST_ASSERT_EQUAL_HEX32(controls[channel], value_of(bm13xx_harness_packet(0)->bytes));
        TEST_ASSERT_EQUAL_HEX32(0x3f014381, value_of(bm13xx_harness_packet(1)->bytes));
        TEST_ASSERT_EQUAL_HEX32(controls[channel] | (1U << 21), value_of(bm13xx_harness_packet(4)->bytes));
        TEST_ASSERT_EQUAL_UINT(10, sample_counts[0]);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 500, BM1370_get_domain_voltage_mv(0, channel, sample_time));
    }
    TEST_ASSERT_TRUE(isnan(BM1370_get_domain_voltage_mv(0, 0, sample_time + 10000001)));
    TEST_ASSERT_TRUE(isnan(BM1370_get_domain_voltage_mv(0, 4, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1370 sensor acquisition rejects missing and unready ADC data", "[asic][temperature][voltage]")
{
    begin_sensor_test();
    missing_chip = true;
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_FALSE(isnan(BM1370_get_die_temperature(0, sample_time)));
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(1, sample_time)));
    bm13xx_harness_clear_packets();
    data_ready = false;
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_TRUE(isnan(BM1370_get_domain_voltage_mv(0, 0, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1370 sensor acquisition stops on power down and failed writes", "[asic][temperature][voltage]")
{
    begin_sensor_test();
    stop_on_start = true;
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(0, sample_time)));
    bm13xx_harness_clear_packets();
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_EQUAL_UINT(0, bm13xx_harness_packet_count());
    stop_on_start = false;
    sensor_state->ASIC_initalized = true;
    bm13xx_harness_fail_writes(1);
    BM1370_read_sensors(sensor_state);
    TEST_ASSERT_TRUE(isnan(BM1370_get_die_temperature(0, sample_time)));
    bm13xx_harness_end();
}
