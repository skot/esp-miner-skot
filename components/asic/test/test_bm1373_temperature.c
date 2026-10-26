#include "unity.h"
#include "bm13xx_test_bindings.h"
#include "bm13xx_test_harness.h"
#include "bm1373.h"
#include "global_state.h"
#include "crc.h"
#include <math.h>

static const uint64_t sample_time = 123456789;

static GlobalState *begin_temperature_test(void)
{
    GlobalState *state = bm13xx_harness_begin(&bm13xx_harness_drivers[3]);
    TEST_ASSERT_EQUAL_UINT8(2, BM1373_init(state));
    bm13xx_harness_clear_packets();
    return state;
}

static void receive_register(GlobalState *state, uint8_t chip_address, uint8_t reg, uint32_t value)
{
    const uint8_t response[BM13XX_HARNESS_RESPONSE_SIZE] = {
        0xaa, 0x55, value >> 24, value >> 16, value >> 8, value,
        chip_address, reg, 0, 0, 0,
    };
    bm13xx_harness_queue_response(response);
    TEST_ASSERT_NULL(BM1373_process_work(state));
}

static uint32_t packet_value(const bm13xx_harness_packet_t *packet)
{
    return ((uint32_t)packet->bytes[6] << 24) | ((uint32_t)packet->bytes[7] << 16) |
           ((uint32_t)packet->bytes[8] << 8) | packet->bytes[9];
}

TEST_CASE("BM1373 temperature polling preserves per-chip config and stock stage order", "[asic][temperature]")
{
    GlobalState *state = begin_temperature_test();
    BM1373_read_registers();
    unsigned control_reads = 0;
    for (size_t i = 0; i < bm13xx_harness_packet_count(); i++) {
        const bm13xx_harness_packet_t *packet = bm13xx_harness_packet(i);
        TEST_ASSERT_FALSE(packet->bytes[2] == 0x41 && packet->bytes[5] == 0xb2);
        if (packet->bytes[5] == 0xb2) {
            TEST_ASSERT_EQUAL_HEX8(0x42, packet->bytes[2]);
            TEST_ASSERT_EQUAL_HEX8(control_reads * 4, packet->bytes[4]);
            control_reads++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, control_reads);

    receive_register(state, 0x00, 0xb2, 0xc0000000);
    receive_register(state, 0x10, 0xb2, 0x4efdffff);
    bm13xx_harness_clear_packets();
    BM1373_read_registers();
    static const uint32_t expected[] = {
        0x40020000, 0x4effffff, 0xc0020000, 0xceffffff,
        0xc1020000, 0xcfffffff, 0xd0020000, 0xdeffffff,
    };
    unsigned writes = 0;
    unsigned data_reads = 0;
    for (size_t i = 0; i < bm13xx_harness_packet_count(); i++) {
        const bm13xx_harness_packet_t *packet = bm13xx_harness_packet(i);
        if (packet->bytes[5] == 0xb2) {
            TEST_ASSERT_LESS_THAN_UINT(8, writes);
            TEST_ASSERT_EQUAL_UINT(11, packet->length);
            TEST_ASSERT_EQUAL_HEX8(0x41, packet->bytes[2]);
            TEST_ASSERT_EQUAL_HEX8((writes % 2) * 4, packet->bytes[4]);
            TEST_ASSERT_EQUAL_HEX32(expected[writes], packet_value(packet));
            TEST_ASSERT_EQUAL_HEX8(crc5((uint8_t *)packet->bytes + 2, 8), packet->bytes[10]);
            writes++;
        } else if (packet->bytes[5] == 0xb3) {
            TEST_ASSERT_EQUAL_UINT(8, writes);
            TEST_ASSERT_EQUAL_HEX8(0x42, packet->bytes[2]);
            TEST_ASSERT_EQUAL_HEX8(data_reads * 4, packet->bytes[4]);
            data_reads++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(8, writes);
    TEST_ASSERT_EQUAL_UINT(2, data_reads);
    TEST_ASSERT_EQUAL_UINT(14, bm13xx_harness_delay_count());
    bm13xx_harness_end();
}

TEST_CASE("BM1373 die temperatures decode both chip addresses with positive slope", "[asic][temperature]")
{
    GlobalState *state = begin_temperature_test();
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(0, sample_time)));
    receive_register(state, 0x00, 0xb3, 0x8000075c);
    receive_register(state, 0x10, 0xb3, 0xa000091d);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.09015f, BM1373_get_die_temperature(0, sample_time));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 99.98787f, BM1373_get_die_temperature(1, sample_time));
    receive_register(state, 0x01, 0xb3, 0x80000000);
    receive_register(state, 0x20, 0xb3, 0x80000000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.09015f, BM1373_get_die_temperature(0, sample_time));
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(2, sample_time)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(255, sample_time)));
    TEST_ASSERT_FALSE(isnan(BM1373_get_die_temperature(1, sample_time + 5000000)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(1, sample_time + 5000001)));
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(1, sample_time - 1)));
    bm13xx_harness_end();
}

TEST_CASE("BM1373 rejects unfinished and failed temperatures and clears them on reset", "[asic][temperature]")
{
    GlobalState *state = begin_temperature_test();
    receive_register(state, 0, 0xb3, 0x800007af);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 38.93539f, BM1373_get_die_temperature(0, sample_time));
    receive_register(state, 0, 0xb3, 0xc00007af);
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(0, sample_time)));
    receive_register(state, 0, 0xb3, 0x000007af);
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(0, sample_time)));
    receive_register(state, 0, 0xb3, 0x800006a4);
    TEST_ASSERT_TRUE(BM1373_get_die_temperature(0, sample_time) < 0);
    TEST_ASSERT_EQUAL_UINT8(2, BM1373_init(state));
    TEST_ASSERT_TRUE(isnan(BM1373_get_die_temperature(0, sample_time)));
    bm13xx_harness_end();
}

TEST_CASE("BM1373 does not read temperature after a failed sensor setup", "[asic][temperature]")
{
    GlobalState *state = begin_temperature_test();
    receive_register(state, 0, 0xb2, 0xc0000000);
    receive_register(state, 0x10, 0xb2, 0xc0000000);
    bm13xx_harness_fail_writes(36);
    BM1373_read_registers();
    for (size_t i = 0; i < bm13xx_harness_packet_count(); i++) {
        TEST_ASSERT_NOT_EQUAL_HEX8(0xb3, bm13xx_harness_packet(i)->bytes[5]);
    }
    bm13xx_harness_clear_packets();
    bm13xx_harness_fail_writes(0);
    BM1373_read_registers();
    bool retried = false;
    for (size_t i = 0; i < bm13xx_harness_packet_count(); i++) {
        if (bm13xx_harness_packet(i)->bytes[5] == 0xb3) retried = true;
    }
    TEST_ASSERT_TRUE(retried);
    bm13xx_harness_end();
}
