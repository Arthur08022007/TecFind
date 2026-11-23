#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include "nav.h"
#include "tec.h"
#include "tec_internal.h"
#include "view_data.h"

LV_FONT_DECLARE(tec_font_semibold_16);
LV_FONT_DECLARE(tec_font_regular_18);
LV_FONT_DECLARE(tec_font_bold_24);
LV_FONT_DECLARE(tec_font_bold_30);
LV_FONT_DECLARE(tec_font_bold_34);
LV_IMAGE_DECLARE(tec_img_bus);

#define TEC_CARD_COUNT 3
#define COLOR_BG        0x141517
#define COLOR_CARD      0x232427
#define COLOR_CARD_LINE 0x34363A
#define COLOR_TEXT      0xF5F6F7
#define COLOR_MUTED     0x8E9299
#define COLOR_DIM       0x5F636A
#define COLOR_SOON      0x34C759
#define COLOR_LATE      0xFF9F0A
#define COLOR_CANCELLED 0xFF5A5F
#define COLOR_ACCENT    0x2F6BFF
#define SOON_MINUTES    5

typedef struct {
    lv_obj_t *card;
    lv_obj_t *badge;
    lv_obj_t *line;
    lv_obj_t *destination;
    lv_obj_t *detail;
    lv_obj_t *minutes;
} departure_card_t;

static const char *TAG = "tec-view";
static const uint32_t LINE_PALETTE[] = {
    0x2F6BFF, 0x22A45D, 0xFF7A1A, 0x8E5CF7, 0xE5484D, 0x12A4B5, 0xE0458B, 0xC99A06,
};

static struct view_data_tec_departures s_data;
static time_t s_updated_at;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi;
static lv_obj_t *s_stop;
static lv_obj_t *s_subtitle;
static lv_obj_t *s_message;
static lv_obj_t *s_footer;
static lv_obj_t *s_footer_text;
static departure_card_t s_cards[TEC_CARD_COUNT];
static lv_obj_t *s_picker;

static uint32_t line_color(const struct view_data_tec_departure *item)
{
    if (item->line_color >= 0) return (uint32_t)item->line_color;
    uint32_t hash = 5381;
    for (const char *c = item->line; *c; c++) hash = hash * 33 + (unsigned char)*c;
    return LINE_PALETTE[hash % (sizeof(LINE_PALETTE) / sizeof(LINE_PALETTE[0]))];
}

static uint32_t contrast_text(uint32_t rgb)
{
    uint32_t luma = ((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114;
    return luma > 160000 ? 0x111111 : 0xFFFFFF;
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static void create_card(lv_obj_t *parent, departure_card_t *card, int index)
{
    card->card = plain_obj(parent);
    lv_obj_set_size(card->card, 440, 92);
    lv_obj_set_pos(card->card, 20, 136 + index * 102);
    lv_obj_set_style_bg_color(card->card, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card->card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card->card, lv_color_hex(COLOR_CARD_LINE), LV_PART_MAIN);
    lv_obj_set_style_border_width(card->card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card->card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card->card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_column(card->card, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(card->card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card->card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    card->badge = plain_obj(card->card);
    lv_obj_set_size(card->badge, LV_SIZE_CONTENT, 64);
    lv_obj_set_style_min_width(card->badge, 64, LV_PART_MAIN);
    lv_obj_set_style_max_width(card->badge, 112, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card->badge, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(card->badge, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card->badge, LV_OPA_COVER, LV_PART_MAIN);
    card->line = text(card->badge, &tec_font_bold_34, 0xFFFFFF);
    lv_label_set_long_mode(card->line, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_max_width(card->line, 96, LV_PART_MAIN);
    lv_obj_center(card->line);

    lv_obj_t *column = plain_obj(card->card);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 2, LV_PART_MAIN);
    card->destination = text(column, &tec_font_bold_24, COLOR_TEXT);
    lv_obj_set_width(card->destination, lv_pct(100));
    lv_label_set_long_mode(card->destination, LV_LABEL_LONG_DOT);
    card->detail = text(column, &tec_font_regular_18, COLOR_MUTED);
    lv_obj_set_width(card->detail, lv_pct(100));
    lv_label_set_long_mode(card->detail, LV_LABEL_LONG_DOT);

    card->minutes = text(card->card, &tec_font_bold_34, COLOR_TEXT);
    lv_obj_set_style_text_align(card->minutes, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

static void render_card(departure_card_t *card, const struct view_data_tec_departure *item, time_t now)
{
    uint32_t badge = line_color(item);
    lv_obj_set_style_bg_color(card->badge, lv_color_hex(badge), LV_PART_MAIN);
    lv_obj_set_style_text_color(card->line, lv_color_hex(contrast_text(badge)), LV_PART_MAIN);
    lv_label_set_text(card->line, item->line);
    lv_label_set_text(card->destination, item->destination[0] ? item->destination : "TEC");

    char detail[48];
    snprintf(detail, sizeof(detail), "%s · %s", item->time, item->status);
    lv_label_set_text(card->detail, detail);
    uint32_t detail_color = item->cancelled ? COLOR_CANCELLED : item->delayed ? COLOR_LATE : COLOR_MUTED;
    lv_obj_set_style_text_color(card->detail, lv_color_hex(detail_color), LV_PART_MAIN);

    long seconds = (long)(item->departure_epoch - now);
    long minutes = seconds > 0 ? seconds / 60 : 0;
    char minutes_text[16];
    uint32_t minutes_color = COLOR_TEXT;
    if (item->cancelled) {
        strlcpy(minutes_text, "Annulé", sizeof(minutes_text));
        minutes_color = COLOR_CANCELLED;
    } else if (minutes < 1) {
        strlcpy(minutes_text, "< 1 min", sizeof(minutes_text));
        minutes_color = COLOR_SOON;
    } else if (minutes < 60) {
        snprintf(minutes_text, sizeof(minutes_text), "%ld min", minutes);
        if (minutes <= SOON_MINUTES) minutes_color = COLOR_SOON;
    } else {
        strlcpy(minutes_text, item->time, sizeof(minutes_text));
    }
    lv_label_set_text(card->minutes, minutes_text);
    lv_obj_set_style_text_color(card->minutes, lv_color_hex(minutes_color), LV_PART_MAIN);
    lv_obj_set_style_text_decor(card->destination,
        item->cancelled ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, LV_PART_MAIN);
}

static void render(void)
{
    time_t now = time(NULL);
    bool clock_valid = now > 1735689600;
    if (clock_valid) {
        struct tm local;
        localtime_r(&now, &local);
        lv_label_set_text_fmt(s_clock, "%02d:%02d", local.tm_hour, local.tm_min);
    } else {
        lv_label_set_text(s_clock, "--:--");
    }

    lv_label_set_text(s_stop, s_data.stop_name[0] ? s_data.stop_name :
                      indicator_tec_stop_name(indicator_tec_selected_stop()));
    bool has_departures = s_data.count > 0;
    lv_label_set_text(s_subtitle, !has_departures ? "Prochains départs" :
                      s_data.realtime ? "Départs en temps réel" : "Horaires planifiés");

    for (int i = 0; i < TEC_CARD_COUNT; i++) {
        if (i < s_data.count) {
            render_card(&s_cards[i], &s_data.items[i], now);
            lv_obj_remove_flag(s_cards[i].card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cards[i].card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (has_departures) {
        lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_message, s_data.status[0] ? s_data.status : "Connexion au flux TEC…");
        lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    }

    if (!has_departures || !s_updated_at || !clock_valid) {
        lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    long age = (long)(now - s_updated_at) / 60;
    if (age < 1) lv_label_set_text(s_footer_text, "Mis à jour à l’instant");
    else lv_label_set_text_fmt(s_footer_text, "Mis à jour il y a %ld min", age);
    lv_obj_remove_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
}

static void tick(lv_timer_t *timer)
{
    render();
}

static void picker_close(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
}

static void picker_open(lv_event_t *event)
{
    (void)event;
    lv_obj_t *list = lv_obj_get_child(s_picker, 1);
    uint16_t selected = indicator_tec_selected_stop();
    for (uint32_t i = 0; i < lv_obj_get_child_count(list); i++) {
        lv_obj_t *option = lv_obj_get_child(list, i);
        lv_obj_set_style_border_width(option, i == selected ? 2 : 0, LV_PART_MAIN);
    }
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_picker);
}

static void stop_option_selected(lv_event_t *event)
{
    uint16_t index = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    if (index == indicator_tec_selected_stop() && s_data.count > 0) return;
    indicator_tec_select_stop(index);
    memset(&s_data, 0, sizeof(s_data));
    strlcpy(s_data.stop_name, indicator_tec_stop_name(index), sizeof(s_data.stop_name));
    strlcpy(s_data.status, "Mise à jour des horaires…", sizeof(s_data.status));
    s_updated_at = 0;
    render();
}

static void create_stop_picker(lv_obj_t *tile)
{
    s_picker = plain_obj(tile);
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(s_picker, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_picker, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker, 20, LV_PART_MAIN);
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = text(s_picker, &tec_font_bold_30, COLOR_TEXT);
    lv_label_set_text(title, "Choisir un arrêt");
    lv_obj_set_pos(title, 4, 4);

    lv_obj_t *list = plain_obj(s_picker);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(list, lv_pct(100), 316);
    lv_obj_set_pos(list, 0, 52);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for (uint16_t i = 0; i < indicator_tec_stop_count(); i++) {
        lv_obj_t *option = plain_obj(list);
        lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(option, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_size(option, lv_pct(100), 56);
        lv_obj_set_style_bg_color(option, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, lv_color_hex(COLOR_CARD_LINE), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(option, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
        lv_obj_set_style_radius(option, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(option, 16, LV_PART_MAIN);
        lv_obj_add_event_cb(option, stop_option_selected, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *name = text(option, &tec_font_regular_18, COLOR_TEXT);
        lv_label_set_text(name, indicator_tec_stop_name(i));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 300);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *distance = text(option, &tec_font_semibold_16, COLOR_MUTED);
        lv_label_set_text_fmt(distance, "%u m", indicator_tec_stop_distance_m(i));
        lv_obj_align(distance, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *close = plain_obj(s_picker);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(close, 160, 44);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, 4);
    lv_obj_set_style_bg_color(close, lv_color_hex(COLOR_CARD_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(close, 22, LV_PART_MAIN);
    lv_obj_add_event_cb(close, picker_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = text(close, &tec_font_semibold_16, COLOR_TEXT);
    lv_label_set_text(close_label, "Fermer");
    lv_obj_center(close_label);
}

static void tec_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    if (event_data == NULL) return;
    lv_port_sem_take();
    if (id == VIEW_EVENT_TEC_DEPARTURES) {
        s_data = *(const struct view_data_tec_departures *)event_data;
        if (s_data.count > 0) s_updated_at = time(NULL);
        render();
    } else if (id == VIEW_EVENT_WIFI_ST) {
        const struct view_data_wifi_st *wifi = event_data;
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(wifi->is_connected ? COLOR_MUTED : COLOR_CANCELLED),
                                    LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(tile, lv_color_hex(COLOR_BG), LV_PART_MAIN);

    lv_obj_t *brand = text(tile, &tec_font_semibold_16, COLOR_DIM);
    lv_label_set_text(brand, "SENSECAP");
    lv_obj_set_style_text_letter_space(brand, 1, LV_PART_MAIN);
    lv_obj_set_pos(brand, 24, 16);

    s_clock = text(tile, &tec_font_semibold_16, COLOR_MUTED);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_set_width(s_clock, 56);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -24, 16);
    s_wifi = text(tile, &lv_font_montserrat_14, COLOR_MUTED);
    lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi, LV_ALIGN_TOP_RIGHT, -24 - 56 - 14, 18);

    lv_obj_t *icon = lv_image_create(tile);
    lv_image_set_src(icon, &tec_img_bus);
    lv_obj_set_pos(icon, 24, 54);

    s_stop = text(tile, &tec_font_bold_30, COLOR_TEXT);
    lv_label_set_text(s_stop, indicator_tec_stop_name(indicator_tec_selected_stop()));
    lv_obj_set_pos(s_stop, 104, 52);
    lv_obj_set_size(s_stop, 352, lv_font_get_line_height(&tec_font_bold_30));
    lv_label_set_long_mode(s_stop, LV_LABEL_LONG_DOT);

    s_subtitle = text(tile, &tec_font_regular_18, COLOR_MUTED);
    lv_obj_set_pos(s_subtitle, 104, 98);
    lv_obj_set_size(s_subtitle, 352, lv_font_get_line_height(&tec_font_regular_18));
    lv_label_set_long_mode(s_subtitle, LV_LABEL_LONG_DOT);

    if (indicator_tec_stop_count() > 1) {
        lv_obj_set_width(s_subtitle, 244);
        lv_obj_t *change = plain_obj(tile);
        lv_obj_set_size(change, 92, 28);
        lv_obj_set_pos(change, 364, 96);
        lv_obj_set_style_bg_color(change, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(change, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(change, lv_color_hex(COLOR_CARD_LINE), LV_PART_MAIN);
        lv_obj_set_style_border_width(change, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(change, 14, LV_PART_MAIN);
        lv_obj_t *change_label = text(change, &tec_font_semibold_16, COLOR_TEXT);
        lv_label_set_text(change_label, "Changer");
        lv_obj_center(change_label);

        /* The whole header (icon, stop name, subtitle, pill) opens the picker. */
        lv_obj_t *header_hit = plain_obj(tile);
        lv_obj_add_flag(header_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(header_hit, 456, 76);
        lv_obj_set_pos(header_hit, 12, 48);
        lv_obj_add_event_cb(header_hit, picker_open, LV_EVENT_CLICKED, NULL);
    }

    for (int i = 0; i < TEC_CARD_COUNT; i++) create_card(tile, &s_cards[i], i);

    s_message = text(tile, &tec_font_bold_24, COLOR_MUTED);
    lv_obj_set_width(s_message, 400);
    lv_label_set_long_mode(s_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_message, LV_ALIGN_CENTER, 0, 40);

    s_footer = plain_obj(tile);
    lv_obj_set_size(s_footer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_footer, 8, LV_PART_MAIN);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_t *refresh = text(s_footer, &lv_font_montserrat_16, COLOR_DIM);
    lv_label_set_text(refresh, LV_SYMBOL_REFRESH);
    s_footer_text = text(s_footer, &tec_font_regular_18, COLOR_DIM);

    if (indicator_tec_stop_count() > 1) create_stop_picker(tile);

    render();
    lv_timer_create(tick, 5000, NULL);
    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
        VIEW_EVENT_BASE, VIEW_EVENT_TEC_DEPARTURES, tec_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
        VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, tec_event_handler, NULL, NULL));
    ESP_LOGI(TAG, "TEC dashboard ready");
    return 0;
}

void tec_view_update(const struct view_data_tec_departures *data)
{
    if (data == NULL) return;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TEC_DEPARTURES,
                      data, sizeof(*data), 0);
}
