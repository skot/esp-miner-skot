// Thermal-history control: keep the already-running 300 MHz work and PLLs
// unchanged; cool before the existing supply-cycle/reinitialize/ramp sequence.
static bool proto_compare_cool_after_hot(GlobalState *global, proto_delay_guard_t *guard)
{
    if (global->POWER_MANAGEMENT_MODULE.actual_frequency != 300) return false;
    int64_t deadline = esp_timer_get_time() + 180000000;
    ESP_LOGW(TAG, "COMPARE COOL begin: keep 300MHz work/PLLs unchanged, fan=100%%, target hottest<=42.5C");
    while (esp_timer_get_time() < deadline) {
        FAN_CONTROLLER_set_bench_percent(100);
        proto_bcast_observation_t temperatures = {0};
        if (atomic_load(&guard->tripped) || global->SYSTEM_MODULE.hardware_fault ||
            proto_bcast_command() == 'X' || MC3_read_temperatures(temperatures.temperature_c, 4) != 4) return false;
        float coldest = 100, hottest = 0;
        for (unsigned c = 0; c < 4; c++) {
            float t = temperatures.temperature_c[c];
            if (!isfinite(t) || t <= 0 || t >= 70) return false;
            if (t < coldest) coldest = t;
            if (t > hottest) hottest = t;
        }
        ESP_LOGW(TAG, "COMPARE COOL A1=%.2f A2=%.2f A3=%.2f A4=%.2f; unchanged 300MHz work",
            temperatures.temperature_c[0], temperatures.temperature_c[1],
            temperatures.temperature_c[2], temperatures.temperature_c[3]);
        if (hottest <= 42.5f) {
            if (coldest < 39.5f) return false;
            proto_warm_fan(&temperatures, 0);
            ESP_LOGW(TAG, "COMPARE COOL READY coldest=%.2f hottest=%.2f; resume original power cycle/ramp", coldest, hottest);
            return true;
        }
        if (!proto_warm_deferred_wait(guard, 1000)) return false;
    }
    ESP_LOGE(TAG, "COMPARE COOL timeout; no higher-frequency launch");
    return false;
}
