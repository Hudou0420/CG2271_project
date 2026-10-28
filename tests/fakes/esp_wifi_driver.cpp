#include "esp_wifi.h"
wifi_init_config_t captured_wifi_config;
bool wifi_received_null;
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config) {
    wifi_received_null = !config;
    if (!config) return -21;
    captured_wifi_config = *config;
    return 37; // prove the wrapper propagates driver results unchanged
}
