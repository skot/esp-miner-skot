#include "unity.h"
#include "device_pins.h"
#include "sdkconfig.h"
#undef CONFIG_ENABLE_BAP
#define CONFIG_ENABLE_BAP 1
#undef CONFIG_GPIO_BAP_TX
#define CONFIG_GPIO_BAP_TX 41
#undef CONFIG_GPIO_BAP_RX
#define CONFIG_GPIO_BAP_RX 42
#define device_pins_init bap_override_pins_init
#define BONANZA_1004_CONTROL_PINS bap_override_control_pins
#define BONANZA_1004_UART_PINS bap_override_asic_uart_pins
#define DEFAULT_BAP_PINS bap_override_default_pins
#define DEFAULT_I2C_PINS bap_override_i2c_pins
#define BITAXE_COLOR_I2C_PINS bap_override_color_i2c_pins
#define ST7789_I80_PINS bap_override_i80_pins
#include "main/device_pins.c"

TEST_CASE("BAP Kconfig pin overrides preserve the Bonanza ASIC UART", "[asic][bonanza][bap][pins]")
{
    DevicePins pins = {.bap = &DEFAULT_BAP_PINS, .bzm = &BONANZA_1004_CONTROL_PINS,
                       .asic_uart = &BONANZA_1004_UART_PINS, .i2c = &DEFAULT_I2C_PINS};
    TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&pins));
    TEST_ASSERT_EQUAL(41, pins.bap->tx);
    TEST_ASSERT_EQUAL(42, pins.bap->rx);
    TEST_ASSERT_EQUAL_PTR(&BONANZA_1004_UART_PINS, pins.asic_uart);
    BzmPins conflicting = BONANZA_1004_CONTROL_PINS;
    conflicting.asic_reset = 41;
    pins.bzm = &conflicting;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, device_pins_init(&pins));
}
