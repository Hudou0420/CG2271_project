#pragma once
using esp_err_t = int;
struct wifi_init_config_t {
    int static_rx_buf_num, dynamic_rx_buf_num, tx_buf_type;
    int static_tx_buf_num, dynamic_tx_buf_num, cache_tx_buf_num;
    int ampdu_rx_enable, ampdu_tx_enable, amsdu_tx_enable, rx_ba_win;
    int nvs_enable, magic;
};
extern "C" esp_err_t esp_wifi_init(const wifi_init_config_t *config);
extern wifi_init_config_t captured_wifi_config;
extern bool wifi_received_null;
