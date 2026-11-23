#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include "nav.h"
#include "tec.h"
#include "tec_internal.h"
#include "view_data.h"

static const char *TAG = "tec-view";
LV_FONT_DECLARE(tec_font_16);
LV_FONT_DECLARE(tec_font_24);
LV_FONT_DECLARE(tec_font_32);
static lv_obj_t *s_stop;
static lv_obj_t *s_status;
static lv_obj_t *s_rows[4];
static lv_obj_t *s_picker;

static void picker_close(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
}

static void picker_open(lv_event_t *event)
{
    (void)event;
    lv_obj_clear_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
}

static void stop_option_selected(lv_event_t *event)
{
    uint16_t index = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    indicator_tec_select_stop(index);
    lv_label_set_text(s_stop, indicator_tec_stop_name(index));
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; i++) lv_label_set_text(s_rows[i], "");
    lv_label_set_text(s_status, "Mise à jour des horaires…");
}

static void create_stop_picker(lv_obj_t *tile)
{
    s_picker = lv_obj_create(tile);
    lv_obj_set_size(s_picker, 440, 350);
    lv_obj_set_pos(s_picker, 20, 70);
    lv_obj_set_style_bg_color(s_picker, lv_color_hex(0x202830), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_picker, lv_color_hex(0x93A4B8), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_picker, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker, 12, LV_PART_MAIN);
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_picker);
    lv_label_set_text(title, "Choisir un arrêt proche");
    lv_obj_set_pos(title, 12, 8);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F5F8), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &tec_font_24, LV_PART_MAIN);

    lv_obj_t *list = lv_obj_create(s_picker);
    lv_obj_set_size(list, 404, 250);
    lv_obj_set_pos(list, 4, 48);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x151B20), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for (uint16_t i = 0; i < indicator_tec_stop_count(); i++) {
        lv_obj_t *button = lv_button_create(list);
        lv_obj_set_size(button, 380, 42);
        lv_obj_add_event_cb(button, stop_option_selected, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        char option[128];
        snprintf(option, sizeof(option), "%s · %u m", indicator_tec_stop_name(i),
                 indicator_tec_stop_distance_m(i));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, option);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 360);
        lv_obj_set_style_text_font(label, &tec_font_16, LV_PART_MAIN);
        lv_obj_center(label);
    }

    lv_obj_t *close = lv_button_create(s_picker);
    lv_obj_set_size(close, 110, 34);
    lv_obj_set_pos(close, 153, 299);
    lv_obj_add_event_cb(close, picker_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Fermer");
    lv_obj_set_style_text_font(close_label, &tec_font_16, LV_PART_MAIN);
    lv_obj_center(close_label);
}

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
    lv_obj_set_width(title, 300);
    lv_obj_set_style_text_color(title, lv_color_hex(0x93A4B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);

    if (indicator_tec_stop_count() > 1) {
        lv_obj_t *choose = lv_button_create(tile);
        lv_obj_set_size(choose, 108, 32);
        lv_obj_set_pos(choose, 348, 13);
        lv_obj_add_event_cb(choose, picker_open, LV_EVENT_CLICKED, NULL);
        lv_obj_t *choose_label = lv_label_create(choose);
        lv_label_set_text(choose_label, "CHOISIR");
        lv_obj_set_style_text_font(choose_label, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_center(choose_label);
    }

    s_stop = lv_label_create(tile);
    lv_label_set_text(s_stop, indicator_tec_stop_name(indicator_tec_selected_stop()));
    lv_obj_set_pos(s_stop, 24, 47);
    lv_obj_set_size(s_stop, 432, 64);
    lv_label_set_long_mode(s_stop, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_stop, lv_color_hex(0xF2F5F8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_stop, &tec_font_24, LV_PART_MAIN);

    lv_obj_t *divider = lv_obj_create(tile);
    lv_obj_set_size(divider, 430, 2);
    lv_obj_set_pos(divider, 24, 113);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x354251), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);

    for (int i = 0; i < 4; i++) {
        s_rows[i] = lv_label_create(tile);
        lv_label_set_text(s_rows[i], "");
        lv_obj_set_pos(s_rows[i], 24, 128 + i * 58);
        lv_obj_set_width(s_rows[i], 432);
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(0xF2F5F8), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_rows[i], &tec_font_24, LV_PART_MAIN);
    }

    s_status = lv_label_create(tile);
    lv_label_set_text(s_status, "Connexion au flux TEC…");
    lv_obj_set_pos(s_status, 24, 390);
    lv_obj_set_width(s_status, 432);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFC857), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &tec_font_16, LV_PART_MAIN);

    if (indicator_tec_stop_count() > 1) create_stop_picker(tile);

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
