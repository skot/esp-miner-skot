#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "main/global_state.h"
#define TEST_STUB_GLOBAL_STATE_H

// Capture what BAP_send_ap_message() puts on the wire.
#define uart_write_bytes ap_mode_test_uart_write
#define TAG ap_mode_uart_tag
#include "main/bap/bap_uart.c"
#undef TAG

static char wire[256];
static size_t wire_len;

int ap_mode_test_uart_write(uart_port_t uart_num, const void *src, size_t size)
{
    TEST_ASSERT_EQUAL(BAP_UART_NUM, uart_num);
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(wire) - 1 - wire_len, size);
    memcpy(wire + wire_len, src, size);
    wire_len += size;
    wire[wire_len] = '\0';
    return (int)size;
}

// Frame a sentence with its own XOR checksum, independent of bap_protocol.c.
static void framed(char *out, size_t out_size, const char *body)
{
    unsigned checksum = 0;
    for (const char *c = body; *c; ++c) checksum ^= (unsigned char)*c;
    snprintf(out, out_size, "$%s*%02X\r\n", body, checksum);
}

static void send_ap_message(GlobalState *state)
{
    wire_len = 0;
    wire[0] = '\0';
    bap_uart_send_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(bap_uart_send_mutex);
    BAP_send_ap_message(state);
    vSemaphoreDelete(bap_uart_send_mutex);
    bap_uart_send_mutex = NULL;
}

TEST_CASE("BAP AP mode message names the setup network while it is up", "[bap][ap_mode]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    snprintf(state->SYSTEM_MODULE.ap_ssid, sizeof(state->SYSTEM_MODULE.ap_ssid), "Bitaxe_A1B2");
    state->SYSTEM_MODULE.ap_enabled = true;

    char mode[64], name[64], expected[128];
    framed(mode, sizeof(mode), "BAP,CMD,mode,ap_mode");
    framed(name, sizeof(name), "BAP,RES,apSsid,Bitaxe_A1B2");

    send_ap_message(state);
    snprintf(expected, sizeof(expected), "%s%s", mode, name);
    TEST_ASSERT_EQUAL_STRING(expected, wire);

    // Access point switched off while WiFi is still down: only the mode.
    state->SYSTEM_MODULE.ap_enabled = false;
    send_ap_message(state);
    TEST_ASSERT_EQUAL_STRING(mode, wire);

    // No name yet.
    state->SYSTEM_MODULE.ap_enabled = true;
    state->SYSTEM_MODULE.ap_ssid[0] = '\0';
    send_ap_message(state);
    TEST_ASSERT_EQUAL_STRING(mode, wire);

    send_ap_message(NULL);
    TEST_ASSERT_EQUAL_STRING(mode, wire);

    free(state);
}
