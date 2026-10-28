#include <esp_wifi.h>
#include <cassert>
#include <cstring>
#include <cstdio>
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config);
int main() {
    wifi_init_config_t stock{4, 32, 1, 0, 32, 4, 1, 1, 0, 16, 1, 12345};
    const auto original = stock;
    assert(__wrap_esp_wifi_init(&stock) == 37); // driver result must propagate unchanged
    assert(!wifi_received_null);
    assert(std::memcmp(&stock, &original, sizeof(stock)) == 0);
    assert(captured_wifi_config.static_rx_buf_num == 2);
    assert(captured_wifi_config.dynamic_rx_buf_num == 8);
    assert(captured_wifi_config.dynamic_tx_buf_num == 8);
    assert(captured_wifi_config.cache_tx_buf_num == 2);
    assert(captured_wifi_config.ampdu_rx_enable == 0);
    assert(captured_wifi_config.ampdu_tx_enable == 0);
    assert(captured_wifi_config.amsdu_tx_enable == 0);
    assert(captured_wifi_config.rx_ba_win == 0);
    assert(captured_wifi_config.nvs_enable == stock.nvs_enable);
    assert(captured_wifi_config.magic == stock.magic);
    stock.static_rx_buf_num = 1;
    stock.dynamic_rx_buf_num = 4;
    stock.dynamic_tx_buf_num = 4;
    stock.cache_tx_buf_num = 1;
    assert(__wrap_esp_wifi_init(&stock) == 37);
    assert(captured_wifi_config.static_rx_buf_num == 1);
    assert(captured_wifi_config.dynamic_rx_buf_num == 4);
    assert(captured_wifi_config.dynamic_tx_buf_num == 4);
    assert(captured_wifi_config.cache_tx_buf_num == 1);
    stock.tx_buf_type = 0; stock.static_tx_buf_num = 8;
    stock.dynamic_tx_buf_num = 32; stock.cache_tx_buf_num = 16;
    stock.dynamic_rx_buf_num = 0;
    assert(__wrap_esp_wifi_init(&stock) == 37);
    assert(captured_wifi_config.dynamic_rx_buf_num == 8); // unlimited RX bounded
    assert(captured_wifi_config.static_tx_buf_num == 8); // preserve static TX mode
    assert(captured_wifi_config.dynamic_tx_buf_num == 32);
    assert(captured_wifi_config.cache_tx_buf_num == 16);
    assert(__wrap_esp_wifi_init(nullptr) == -21 && wifi_received_null);
    puts("Wi-Fi initializer wrapper bounds buffers, preserves driver config and propagates errors");
}
