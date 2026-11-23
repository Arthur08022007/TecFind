#pragma once

#include <stdbool.h>
#include <stdint.h>

enum start_screen {
    SCREEN_SENSECAP_LOG = 0,
    SCREEN_WIFI_CONFIG = 1,
    SCREEN_DISPLAY_MODAL = 2,
};

#define WIFI_SCAN_LIST_SIZE 15

struct view_data_wifi_st {
    bool is_connected;
    bool is_connecting;
    bool is_network;
    char ssid[32];
    int8_t rssi;
};

struct view_data_wifi_config {
    char ssid[32];
    uint8_t password[64];
    bool have_password;
};

struct view_data_wifi_item {
    char ssid[32];
    bool auth_mode;
    int8_t rssi;
};

struct view_data_wifi_list {
    bool is_connect;
    struct view_data_wifi_item connect;
    uint16_t cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret;
    char msg[64];
};

struct view_data_display {
    int brightness;
    bool sleep_mode_en;
    int sleep_mode_time_min;
};

struct view_data_tec_departure {
    char line[16];
    char destination[64];
    char time[8];
    char status[32];
    int64_t departure_epoch;
    int32_t line_color;
    bool cancelled;
    bool delayed;
};

struct view_data_tec_departures {
    char stop_name[64];
    char status[96];
    int count;
    bool realtime;
    struct view_data_tec_departure items[4];
};

enum {
    VIEW_EVENT_SCREEN_START = 0,
    VIEW_EVENT_WIFI_ST,
    VIEW_EVENT_TEC_DEPARTURES,
    VIEW_EVENT_WIFI_LIST,
    VIEW_EVENT_WIFI_LIST_START,
    VIEW_EVENT_WIFI_LIST_REQ,
    VIEW_EVENT_WIFI_CONNECT,
    VIEW_EVENT_WIFI_CONNECT_RET,
    VIEW_EVENT_WIFI_CFG_DELETE,
    VIEW_EVENT_DISPLAY_CFG,
    VIEW_EVENT_BRIGHTNESS_UPDATE,
    VIEW_EVENT_DISPLAY_CFG_APPLY,
    VIEW_EVENT_SHUTDOWN,
    VIEW_EVENT_FACTORY_RESET,
    VIEW_EVENT_SCREEN_CTRL,
};

