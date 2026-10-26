#include "unity.h"
#include "bm13xx_test_bindings.h"
#include "bm13xx_test_harness.h"
#include "bm1373.h"
#include "global_state.h"
#include <math.h>

static GlobalState *voltage_state;
static const uint64_t sample_time = 123456789;
static uint8_t channel;
static unsigned samples[2];
static bool data_ready;
static bool missing_chip;
static bool stop_on_start;

static uint32_t packet_value(const uint8_t *bytes)
{
    return ((uint32_t)bytes[6] << 24) | ((uint32_t)bytes[7] << 16) | ((uint32_t)bytes[8] << 8) | bytes[9];
}

static void send_adc_reply(uint8_t address, uint32_t value)
{
    const uint8_t response[BM13XX_HARNESS_RESPONSE_SIZE] = {
        0xaa, 0x55, value >> 24, value >> 16, value >> 8, value, address, 0xbd, 0, 0, 0,
    };
    bm13xx_harness_queue_response(response);
    TEST_ASSERT_NULL(BM1373_process_work(voltage_state));
}

static void adc_reply_hook(const uint8_t *bytes, size_t length)
{
    if (length == 11 && bytes[5] == 0xb8 && (packet_value(bytes) & (1U << 21)) && stop_on_start) {
        voltage_state->ASIC_initalized = false;
    }
    if (length != 7 || bytes[2] != 0x42 || bytes[5] != 0xbd) return;
    TEST_ASSERT_TRUE(bytes[4] == 0 || bytes[4] == 0x10);
    uint8_t chip = bytes[4] / 0x10;
    if (chip && missing_chip) return;
    uint32_t code = chip ? (channel < 2 ? 24576 : 20480) : (channel < 2 ? 28399 : 22395);
    bool discarded = samples[chip]++ == 0;
    if (discarded) code = 16384;
    send_adc_reply(bytes[4], (data_ready && !discarded ? 0x80000000 : 0) | 0x21238000 | code);
}

static void begin_voltage_test(void)
{
    voltage_state = bm13xx_harness_begin(&bm13xx_harness_drivers[3]);
    TEST_ASSERT_EQUAL_UINT8(2, BM1373_init(voltage_state));
    voltage_state->ASIC_initalized = true;
    data_ready = true;
    missing_chip = false;
    stop_on_start = false;
    channel = 0;
    samples[0] = samples[1] = 0;
    bm13xx_harness_clear_packets();
    bm13xx_harness_set_serial_hook(adc_reply_hook);
}

TEST_CASE("BM1373 voltage ADC averages nine samples and selects all four channels", "[asic][voltage]")
{
    begin_voltage_test();
    static const uint32_t initial[] = {0x230c200d, 0x230c400d, 0x230c800d, 0x230ca00d};
    for (channel = 0; channel < 4; channel++) {
        samples[0] = samples[1] = 0;
        bm13xx_harness_clear_packets();
        BM1373_read_voltages(voltage_state);
        TEST_ASSERT_EQUAL_UINT(34, bm13xx_harness_packet_count());
        TEST_ASSERT_EQUAL_HEX8(0xb8, bm13xx_harness_packet(0)->bytes[5]);
        TEST_ASSERT_EQUAL_HEX32(initial[channel], packet_value(bm13xx_harness_packet(0)->bytes));
        TEST_ASSERT_EQUAL_HEX32(0x3f014081, packet_value(bm13xx_harness_packet(1)->bytes));
        TEST_ASSERT_EQUAL_HEX32(0x00040010, packet_value(bm13xx_harness_packet(2)->bytes));
        TEST_ASSERT_EQUAL_HEX32(0x03340e80, packet_value(bm13xx_harness_packet(3)->bytes));
        for (unsigned sample = 0; sample < 10; sample++) {
            TEST_ASSERT_EQUAL_HEX32(initial[channel] | (1U << 21), packet_value(bm13xx_harness_packet(4 + sample * 3)->bytes));
        }
        TEST_ASSERT_EQUAL_UINT(10, samples[0]);
        TEST_ASSERT_EQUAL_UINT(10, samples[1]);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, channel < 2 ? 440.002441f : 440.258789f,
                                 BM1373_get_domain_voltage_mv(0, channel, sample_time));
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 300, BM1373_get_domain_voltage_mv(1, channel, sample_time));
    }
    TEST_ASSERT_FALSE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time + 10000000)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time + 10000001)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time - 1)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(2, 0, sample_time)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 4, sample_time)));
    send_adc_reply(0, 0x80007fff);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 440, BM1373_get_domain_voltage_mv(0, 0, sample_time));
    TEST_ASSERT_EQUAL_UINT8(2, BM1373_init(voltage_state));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1373 voltage ADC rejects unready samples and isolates a missing chip", "[asic][voltage]")
{
    begin_voltage_test();
    data_ready = false;
    BM1373_read_voltages(voltage_state);
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(1, 0, sample_time)));
    data_ready = true;
    missing_chip = true;
    channel = 1;
    samples[0] = samples[1] = 0;
    bm13xx_harness_clear_packets();
    BM1373_read_voltages(voltage_state);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 440, BM1373_get_domain_voltage_mv(0, 1, sample_time));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(1, 1, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1373 voltage ADC stops acquisition when ASIC power is disabled", "[asic][voltage]")
{
    begin_voltage_test();
    stop_on_start = true;
    BM1373_read_voltages(voltage_state);
    TEST_ASSERT_EQUAL_UINT(5, bm13xx_harness_packet_count());
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time)));
    bm13xx_harness_clear_packets();
    BM1373_read_voltages(voltage_state);
    TEST_ASSERT_EQUAL_UINT(0, bm13xx_harness_packet_count());
    bm13xx_harness_end();
}

TEST_CASE("BM1373 voltage ADC invalidates acquisition after a configuration write failure", "[asic][voltage]")
{
    begin_voltage_test();
    for (channel = 0; channel < 4; channel++) {
        samples[0] = samples[1] = 0;
        bm13xx_harness_clear_packets();
        BM1373_read_voltages(voltage_state);
    }
    TEST_ASSERT_FALSE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time)));
    bm13xx_harness_clear_packets();
    bm13xx_harness_fail_writes(3);
    BM1373_read_voltages(voltage_state);
    TEST_ASSERT_EQUAL_UINT(3, bm13xx_harness_packet_count());
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(0, 0, sample_time)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_domain_voltage_mv(1, 0, sample_time)));
    bm13xx_harness_end();
}
