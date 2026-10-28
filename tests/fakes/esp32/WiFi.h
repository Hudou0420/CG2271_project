#pragma once
#include "Arduino.h"
enum wl_status_t {WL_NO_SHIELD,WL_IDLE_STATUS,WL_NO_SSID_AVAIL,WL_SCAN_COMPLETED,WL_CONNECTED,WL_CONNECT_FAILED,WL_CONNECTION_LOST,WL_DISCONNECTED};
constexpr int WIFI_STA=1;
struct TestIP {String toString() const {return "127.0.0.1";}};
struct TestWiFi {
    wl_status_t connection=WL_CONNECTED;
    wl_status_t status() const {return connection;}
    void mode(int mode) {(void)mode;}
    void setAutoReconnect(bool enabled) {(void)enabled;}
    void begin(const char *ssid,const char *password) {(void)ssid;(void)password;}
    TestIP localIP() const {return {};}
    int RSSI() const {return -30;}
};
inline TestWiFi WiFi;
