#include "unity.h"
#include "device_pins.h"
#include "sdkconfig.h"
#undef CONFIG_ENABLE_BAP
#define device_pins_init bap_disabled_pins_init
#define BONANZA_1004_CONTROL_PINS bap_disabled_control_pins
#define BONANZA_1004_UART_PINS bap_disabled_asic_uart_pins
#define DEFAULT_BAP_PINS bap_disabled_default_pins
#define DEFAULT_I2C_PINS bap_disabled_i2c_pins
#define BITAXE_COLOR_I2C_PINS bap_disabled_color_i2c_pins
#define ST7789_I80_PINS bap_disabled_i80_pins
#include "main/device_pins.c"

TEST_CASE("BAP Kconfig disable releases Bonanza accessory pins", "[asic][bonanza][bap][pins]")
{
    DevicePins pins = {.bap = &DEFAULT_BAP_PINS, .bzm = &BONANZA_1004_CONTROL_PINS,
                       .asic_uart = &BONANZA_1004_UART_PINS, .i2c = &DEFAULT_I2C_PINS};
    TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&pins));
    TEST_ASSERT_NULL(pins.bap);
    TEST_ASSERT_EQUAL_PTR(&BONANZA_1004_UART_PINS, pins.asic_uart);
}
