#!/usr/bin/env python3
"""Exercise the production pin validator and Kconfig overrides on the host."""

from pathlib import Path
import subprocess
import tempfile
import unittest


class BitairPinsTest(unittest.TestCase):
    def test_pin_reservations_and_overrides(self):
        root = Path(__file__).resolve().parents[2]
        stubs = {
            "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
""",
            "soc/gpio_num.h": "#pragma once\ntypedef int gpio_num_t;\n"
                "#define GPIO_NUM_NC -1\n#define GPIO_NUM_MAX 49\n" +
                "\n".join(f"#define GPIO_NUM_{i} {i}" for i in range(49)),
            "driver/gpio.h": "#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) < 49)\n",
            "esp_log.h": """static void test_log(const char *tag, const char *format, ...) {
    (void)tag; (void)format;
}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
""",
            "sdkconfig.h": """extern int bap_tx, bap_rx, i2c_sda, i2c_scl;
#define CONFIG_ENABLE_BAP 1
#define CONFIG_GPIO_BAP_TX bap_tx
#define CONFIG_GPIO_BAP_RX bap_rx
#define CONFIG_GPIO_I2C_SDA i2c_sda
#define CONFIG_GPIO_I2C_SCL i2c_scl
""",
        }
        source = r"""
#include <assert.h>
#include "device_pins.c"
int bap_tx = 4, bap_rx = 5, i2c_sda = 47, i2c_scl = 48;
int main(void) {
    DevicePins pins = BITAIR_PINS;
    assert(device_pins_init(&pins) == ESP_OK);
    assert(pins.bap == NULL && pins.i80 == NULL);
    assert(pins.i2c->sda == 47 && pins.i2c->scl == 48);
    const int reserved[] = {6, 39, 40, 41};
    for (unsigned i = 0; i < sizeof(reserved)/sizeof(reserved[0]); i++) {
        pins = (DevicePins)BITAIR_PINS;
        i2c_sda = reserved[i];
        assert(device_pins_init(&pins) == ESP_ERR_INVALID_STATE);
    }
    pins = (DevicePins)BITAIR_PINS;
    i2c_sda = i2c_scl = 47;
    assert(device_pins_init(&pins) == ESP_ERR_INVALID_STATE);
    i2c_sda = 99; i2c_scl = 48;
    assert(device_pins_init(&pins) == ESP_ERR_INVALID_STATE);
    i2c_sda = 47;
    pins = (DevicePins)BITAXE_ORIGINAL_PINS;
    assert(device_pins_init(&pins) == ESP_OK);
    assert(pins.bap->tx == 4 && pins.bap->rx == 5);
    // Existing custom boards may explicitly enable BAP through Kconfig.
    pins = (DevicePins){0};
    assert(device_pins_init(&pins) == ESP_OK);
    assert(pins.bap->tx == 4 && pins.bap->rx == 5);
    assert(device_pins_init(NULL) == ESP_ERR_INVALID_ARG);
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="bitair-pins-test-") as directory:
            temp = Path(directory)
            for name, content in stubs.items():
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            (temp / "test.c").write_text(source)
            binary = temp / "test-pins"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-include", "stddef.h", "-I", str(temp),
                            "-I", str(root / "main"), str(temp / "test.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
