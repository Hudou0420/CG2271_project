// Arduino fixes its Wi-Fi queue sizes inside WiFiGeneric.cpp. Wrap the public
// IDF initializer so this project can bound them without patching SDK files.
// This device sends small REST requests, not high-throughput aggregated traffic.
#include <esp_wifi.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config) {
    if (!config) return __real_esp_wifi_init(config);
    wifi_init_config_t bounded = *config;
    if (bounded.static_rx_buf_num > 2) bounded.static_rx_buf_num = 2;
    // Zero dynamic RX buffers means unlimited; explicitly bound that case too.
    if (bounded.dynamic_rx_buf_num == 0 || bounded.dynamic_rx_buf_num > 8)
        bounded.dynamic_rx_buf_num = 8;
    if (bounded.tx_buf_type == 1) { // Arduino's dynamic TX mode
        if (bounded.dynamic_tx_buf_num > 8) bounded.dynamic_tx_buf_num = 8;
        if (bounded.cache_tx_buf_num > 2) bounded.cache_tx_buf_num = 2;
    }
    bounded.ampdu_rx_enable = 0;
    bounded.ampdu_tx_enable = 0;
    bounded.amsdu_tx_enable = 0;
    bounded.rx_ba_win = 0; // unused with receive aggregation disabled
#ifdef ARDUINO
    Serial.printf("[wifi] memory profile: staticRX=%d dynamicRX=%d dynamicTX=%d aggregation=off\n",
                  bounded.static_rx_buf_num, bounded.dynamic_rx_buf_num, bounded.dynamic_tx_buf_num);
#endif
    return __real_esp_wifi_init(&bounded);
}
