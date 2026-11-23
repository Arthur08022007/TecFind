#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include "nav.h"
#include "tec_internal.h"
#if __has_include("tec_schedule_generated.h")
#include "tec_schedule_generated.h"
#else
#define TEC_GENERATED_STOP_NAME "Arrêt TEC"
#endif
#include "view_data.h"

static const char *TAG = "tec-view";
LV_FONT_DECLARE(tec_font_16);
LV_FONT_DECLARE(tec_font_24);
LV_FONT_DECLARE(tec_font_32);
static lv_obj_t *s_stop;
static lv_obj_t *s_status;
static lv_obj_t *s_rows[4];

static void tec_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id != VIEW_EVENT_TEC_DEPARTURES || event_data == NULL) return;
    const struct view_data_tec_departures *data = event_data;

    lv_port_sem_take();
    lv_label_set_text(s_stop, data->stop_name);
    lv_label_set_text(s_status, data->status);
    for (int i = 0; i < 4; i++) {
        if (i < data->count) {
            char text[96];
            snprintf(text, sizeof(text), "%s  %-5s  %-12s  %s", data->items[i].time,
                     data->items[i].line, data->items[i].destination, data->items[i].status);
            lv_label_set_text(s_rows[i], text);
            lv_obj_set_style_text_color(s_rows[i],
                data->items[i].cancelled ? lv_color_hex(0xFF6464) : lv_color_hex(0xF2F5F8),
                LV_PART_MAIN);
        } else {
            lv_label_set_text(s_rows[i], "");
        }
    }
    lv_port_sem_give();
}

int indicator_tec_view_init(void)
{
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_TEC);
    if (tile == NULL) {
        lv_port_sem_give();
        return -1;
    }

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "TEC  /  PROCHAINS BUS");
    lv_obj_set_pos(title, 24, 20);
    lv_obj_set_style_text_color(title, lv_color_hex(0x93A4B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);

    s_stop = lv_label_create(tile);
    lv_label_set_text(s_stop, TEC_GENERATED_STOP_NAME);
    lv_obj_set_pos(s_stop, 24, 54);
    lv_obj_set_width(s_stop, 420);
    lv_label_set_long_mode(s_stop, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_stop, lv_color_hex(0xF2F5F8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_stop, &tec_font_32, LV_PART_MAIN);

    lv_obj_t *divider = lv_obj_create(tile);
    lv_obj_set_size(divider, 430, 2);
    lv_obj_set_pos(divider, 24, 104);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x354251), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);

    for (int i = 0; i < 4; i++) {
        s_rows[i] = lv_label_create(tile);
        lv_label_set_text(s_rows[i], "");
        lv_obj_set_pos(s_rows[i], 24, 126 + i * 58);
        lv_obj_set_width(s_rows[i], 432);
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(0xF2F5F8), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_rows[i], &tec_font_24, LV_PART_MAIN);
    }

    s_status = lv_label_create(tile);
    lv_label_set_text(s_status, "Connexion au flux TEC…");
    lv_obj_set_pos(s_status, 24, 387);
    lv_obj_set_width(s_status, 432);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFC857), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &tec_font_16, LV_PART_MAIN);

    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
        VIEW_EVENT_BASE, VIEW_EVENT_TEC_DEPARTURES, tec_event_handler, NULL, NULL));
    ESP_LOGI(TAG, "TEC dashboard ready");
    return 0;
}

void tec_view_update(const struct view_data_tec_departures *data)
{
    if (data == NULL) return;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TEC_DEPARTURES,
                      data, sizeof(*data), 0);
}
