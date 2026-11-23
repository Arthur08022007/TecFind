#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "tec.h"
#include "tec_internal.h"

#if __has_include("tec_schedule_generated.h")
#include "tec_schedule_generated.h"
#if !defined(TEC_STOP_COUNT)
/* Keep an already-generated single-stop table usable until it is regenerated. */
typedef struct { const char *stop_id; const char *name; unsigned int distance_m; } tec_stop_t;
static const tec_stop_t TEC_STOPS[] = {{TEC_GENERATED_STOP_ID, TEC_GENERATED_STOP_NAME, 0}};
#define TEC_STOP_COUNT 1
#define TEC_SCHEDULE_STOP_INDEX(schedule) 0
#else
#define TEC_SCHEDULE_STOP_INDEX(schedule) ((schedule)->stop_index)
#endif
#else
typedef struct { const char *stop_id; const char *name; unsigned int distance_m; } tec_stop_t;
typedef struct { unsigned short stop_index; const char *trip_id; const char *service_id; const char *line; const char *destination; int departure_seconds; } tec_static_departure_t;
typedef struct { const char *service_id; const char *start_date; const char *end_date; unsigned char weekdays; } tec_service_calendar_t;
typedef struct { const char *service_id; const char *date; unsigned char added; } tec_service_exception_t;
#define TEC_STOP_COUNT 0
#define TEC_STATIC_DEPARTURE_COUNT 0
#define TEC_SERVICE_CALENDAR_COUNT 0
#define TEC_SERVICE_EXCEPTION_COUNT 0
#define TEC_SCHEDULE_STOP_INDEX(schedule) ((schedule)->stop_index)
static const tec_stop_t TEC_STOPS[1] = {{0}};
static const tec_static_departure_t TEC_STATIC_DEPARTURES[1] = {{0}};
static const tec_service_calendar_t TEC_SERVICE_CALENDAR[1] = {{0}};
static const tec_service_exception_t TEC_SERVICE_EXCEPTIONS[1] = {{0}};
#endif

#ifndef CONFIG_TEC_API_KEY
#define CONFIG_TEC_API_KEY ""
#endif

#define TEC_TRIP_UPDATES_URL "https://api.belgianmobility.io/api/gtfs/feed/tec/rt/trip-update?format=protobuf"
#define TEC_MAX_FEED_BYTES (1024 * 1024)
#define TEC_POLL_SECONDS 60
#define TEC_MAX_REALTIME_TRIPS 1024
#define TEC_NVS_NAMESPACE "tecfind"
#define TEC_NVS_STOP_KEY "stop_id"

static const char *TAG = "tec";

typedef struct { char *data; size_t length; bool too_large; } response_buffer_t;
typedef struct { char trip_id[96]; bool found; bool cancelled; bool has_stop_update; int delay_seconds; int64_t expected_epoch; } realtime_trip_t;
typedef struct { uint32_t number; uint8_t wire; uint64_t integer; const uint8_t *bytes; size_t length; } protobuf_field_t;
static realtime_trip_t s_realtime[TEC_MAX_REALTIME_TRIPS];
static size_t s_realtime_count;

/* strlcpy that never cuts a UTF-8 sequence in half (a split "é" renders as garbage). */
static void utf8_copy(char *dst, const char *src, size_t size)
{
    if (size == 0) return;
    size_t n = strlen(src);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int32_t schedule_line_color(const tec_static_departure_t *schedule)
{
#ifdef TEC_GENERATED_HAS_ROUTE_COLOR
    return schedule->route_color;
#else
    (void)schedule;
    return -1;
#endif
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_buffer_t *buffer = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 || buffer->too_large) return ESP_OK;
    if (buffer->length + event->data_len > TEC_MAX_FEED_BYTES) { buffer->too_large = true; return ESP_FAIL; }
    char *next = heap_caps_realloc(buffer->data, buffer->length + event->data_len,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) return ESP_ERR_NO_MEM;
    buffer->data = next;
    memcpy(next + buffer->length, event->data, event->data_len);
    buffer->length += event->data_len;
    return ESP_OK;
}

static esp_err_t download_updates(response_buffer_t *body)
{
    esp_http_client_config_t config = {.url = TEC_TRIP_UPDATES_URL, .event_handler = http_event,
        .user_data = body, .timeout_ms = 25000, .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    esp_http_client_set_header(client, "bmc-partner-key", CONFIG_TEC_API_KEY);
    esp_http_client_set_header(client, "Accept", "application/x-protobuf");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Trip updates request failed: %s, HTTP %d", esp_err_to_name(err), status);
        return body->too_large ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    return ESP_OK;
}

static bool read_varint(const uint8_t *data, size_t size, size_t *position, uint64_t *value)
{
    *value = 0;
    for (unsigned shift = 0; shift < 64 && *position < size; shift += 7) {
        uint8_t byte = data[(*position)++];
        *value |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) return true;
    }
    return false;
}

static bool next_field(const uint8_t *data, size_t size, size_t *position, protobuf_field_t *field)
{
    if (*position >= size) return false;
    uint64_t key;
    if (!read_varint(data, size, position, &key) || key == 0) return false;
    field->number = key >> 3;
    field->wire = key & 7;
    field->integer = 0;
    field->bytes = NULL;
    field->length = 0;
    if (field->wire == 0) return read_varint(data, size, position, &field->integer);
    if (field->wire == 1 || field->wire == 5) {
        size_t width = field->wire == 1 ? 8 : 4;
        if (size - *position < width) return false;
        field->bytes = data + *position;
        field->length = width;
        *position += width;
        return true;
    }
    if (field->wire == 2) {
        uint64_t length;
        if (!read_varint(data, size, position, &length) || length > size - *position) return false;
        field->bytes = data + *position;
        field->length = (size_t)length;
        *position += (size_t)length;
        return true;
    }
    return false;
}

static bool ids_match(const char *feed_id, const char *static_id)
{
    if (!feed_id || !static_id) return false;
    if (strcmp(feed_id, static_id) == 0) return true;
    size_t a = strlen(feed_id), b = strlen(static_id);
    return a > b && strcmp(feed_id + a - b, static_id) == 0;
}

static const char *selected_stop_id(void)
{
    uint16_t index = indicator_tec_selected_stop();
    return index < TEC_STOP_COUNT ? TEC_STOPS[index].stop_id : "";
}

static void parse_trip_descriptor(const uint8_t *data, size_t size, realtime_trip_t *trip)
{
    size_t pos = 0;
    protobuf_field_t field;
    while (next_field(data, size, &pos, &field)) {
        if (field.number == 1 && field.wire == 2) {
            size_t n = field.length < sizeof(trip->trip_id) - 1 ? field.length : sizeof(trip->trip_id) - 1;
            memcpy(trip->trip_id, field.bytes, n);
            trip->trip_id[n] = '\0';
        } else if (field.number == 4 && field.wire == 0) {
            trip->cancelled = field.integer == 3;
        }
    }
}

static void parse_stop_event(const uint8_t *data, size_t size, int *delay, int64_t *epoch)
{
    size_t pos = 0;
    protobuf_field_t field;
    while (next_field(data, size, &pos, &field)) {
        if (field.number == 1 && field.wire == 0) *delay = (int32_t)field.integer;
        if (field.number == 2 && field.wire == 0) *epoch = (int64_t)field.integer;
    }
}

static void parse_stop_time_update(const uint8_t *data, size_t size, realtime_trip_t *trip)
{
    size_t pos = 0;
    protobuf_field_t field;
    char stop_id[96] = {0};
    int arrival_delay = 0, departure_delay = 0;
    int64_t arrival_epoch = 0, departure_epoch = 0;
    bool skipped = false;
    while (next_field(data, size, &pos, &field)) {
        if (field.number == 2 && field.wire == 2) parse_stop_event(field.bytes, field.length, &arrival_delay, &arrival_epoch);
        else if (field.number == 3 && field.wire == 2) parse_stop_event(field.bytes, field.length, &departure_delay, &departure_epoch);
        else if (field.number == 4 && field.wire == 2) {
            size_t n = field.length < sizeof(stop_id) - 1 ? field.length : sizeof(stop_id) - 1;
            memcpy(stop_id, field.bytes, n);
            stop_id[n] = '\0';
        } else if (field.number == 5 && field.wire == 0) skipped = field.integer == 1;
    }
    if (!ids_match(stop_id, selected_stop_id())) return;
    trip->has_stop_update = true;
    trip->cancelled |= skipped;
    if (departure_epoch || departure_delay) {
        trip->expected_epoch = departure_epoch;
        trip->delay_seconds = departure_delay;
    } else {
        trip->expected_epoch = arrival_epoch;
        trip->delay_seconds = arrival_delay;
    }
}

static void parse_trip_update(const uint8_t *data, size_t size)
{
    realtime_trip_t trip = {0};
    size_t pos = 0;
    protobuf_field_t field;
    while (next_field(data, size, &pos, &field)) {
        if (field.number == 1 && field.wire == 2) parse_trip_descriptor(field.bytes, field.length, &trip);
        else if (field.number == 2 && field.wire == 2) parse_stop_time_update(field.bytes, field.length, &trip);
        else if (field.number == 5 && field.wire == 0) trip.delay_seconds = (int32_t)field.integer;
    }
    if (trip.trip_id[0] && (trip.has_stop_update || trip.cancelled) && s_realtime_count < TEC_MAX_REALTIME_TRIPS) {
        trip.found = true;
        s_realtime[s_realtime_count++] = trip;
    }
}

static bool parse_feed(const uint8_t *data, size_t size)
{
    size_t pos = 0;
    protobuf_field_t field;
    s_realtime_count = 0;
    bool saw_header = false;
    while (next_field(data, size, &pos, &field)) {
        if (field.number == 1 && field.wire == 2) saw_header = true;
        if (field.number == 2 && field.wire == 2) {
            size_t entity_pos = 0;
            protobuf_field_t entity_field;
            while (next_field(field.bytes, field.length, &entity_pos, &entity_field))
                if (entity_field.number == 3 && entity_field.wire == 2)
                    parse_trip_update(entity_field.bytes, entity_field.length);
        }
    }
    return saw_header;
}

static realtime_trip_t find_realtime(const char *trip_id, bool realtime_ok)
{
    realtime_trip_t none = {0};
    if (!realtime_ok) return none;
    for (size_t i = 0; i < s_realtime_count; i++)
        if (ids_match(s_realtime[i].trip_id, trip_id)) return s_realtime[i];
    return none;
}

static bool service_active(const char *service_id, const struct tm *local)
{
    char date[9];
    strftime(date, sizeof(date), "%Y%m%d", local);
    bool active = false;
    int weekday = (local->tm_wday + 6) % 7;
    for (int i = 0; i < TEC_SERVICE_CALENDAR_COUNT; i++) {
        const tec_service_calendar_t *service = &TEC_SERVICE_CALENDAR[i];
        if (strcmp(service->service_id, service_id) == 0 && strcmp(date, service->start_date) >= 0 &&
            strcmp(date, service->end_date) <= 0 && (service->weekdays & (1 << weekday))) {
            active = true;
            break;
        }
    }
    for (int i = 0; i < TEC_SERVICE_EXCEPTION_COUNT; i++) {
        const tec_service_exception_t *exception = &TEC_SERVICE_EXCEPTIONS[i];
        if (strcmp(exception->service_id, service_id) == 0 && strcmp(exception->date, date) == 0)
            active = exception->added != 0;
    }
    return active;
}

static void add_upcoming(struct view_data_tec_departures *out, const tec_static_departure_t *schedule,
                         const struct tm *now_local, time_t now, bool realtime_ok,
                         uint16_t selected_stop_index)
{
    if (TEC_SCHEDULE_STOP_INDEX(schedule) != selected_stop_index) return;
    if (!service_active(schedule->service_id, now_local)) return;
    struct tm scheduled_parts = *now_local;
    scheduled_parts.tm_hour = schedule->departure_seconds / 3600;
    scheduled_parts.tm_min = (schedule->departure_seconds % 3600) / 60;
    scheduled_parts.tm_sec = schedule->departure_seconds % 60;
    scheduled_parts.tm_isdst = -1;
    time_t scheduled = mktime(&scheduled_parts);
    if (scheduled < now - 60 || scheduled > now + 7200) return;

    realtime_trip_t realtime = find_realtime(schedule->trip_id, realtime_ok);
    time_t expected = realtime.expected_epoch ? (time_t)realtime.expected_epoch :
                      scheduled + (realtime.found ? realtime.delay_seconds : 0);
    if (expected < now - 60) return;
    struct tm expected_parts;
    localtime_r(&expected, &expected_parts);
    char expected_text[8];
    strftime(expected_text, sizeof(expected_text), "%H:%M", &expected_parts);
    int position = out->count;
    while (position > 0 && out->items[position - 1].departure_epoch > (int64_t)expected) {
        if (position < 4) out->items[position] = out->items[position - 1];
        position--;
    }
    if (position >= 4) return;
    struct view_data_tec_departure *item = &out->items[position];
    memset(item, 0, sizeof(*item));
    utf8_copy(item->line, schedule->line, sizeof(item->line));
    utf8_copy(item->destination, schedule->destination, sizeof(item->destination));
    strlcpy(item->time, expected_text, sizeof(item->time));
    item->departure_epoch = expected;
    item->line_color = schedule_line_color(schedule);
    item->cancelled = realtime.cancelled;
    item->delayed = !realtime.cancelled && realtime.found && realtime.delay_seconds >= 60;
    if (realtime.cancelled) strlcpy(item->status, "Supprimé", sizeof(item->status));
    else if (item->delayed)
        snprintf(item->status, sizeof(item->status), "Retard +%d min", (realtime.delay_seconds + 30) / 60);
    else if (realtime.found) strlcpy(item->status, "Temps réel", sizeof(item->status));
    else strlcpy(item->status, "Théorique", sizeof(item->status));
    if (out->count < 4) out->count++;
}

static void refresh_screen(bool realtime_ok)
{
    struct view_data_tec_departures data = {0};
    utf8_copy(data.stop_name, TEC_GENERATED_STOP_NAME, sizeof(data.stop_name));
    data.realtime = realtime_ok;
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    for (int i = 0; i < TEC_STATIC_DEPARTURE_COUNT; i++)
        add_upcoming(&data, &TEC_STATIC_DEPARTURES[i], &local, now, realtime_ok, selected_stop_index);
    if (!TEC_STOP_COUNT)
        strlcpy(data.status, "Générez la liste des arrêts avec tools/find_stop.py", sizeof(data.status));
    else if (realtime_ok)
        strlcpy(data.status, "Temps réel TEC · Belgian Mobility", sizeof(data.status));
    else
        strlcpy(data.status, "Horaires planifiés · flux temps réel indisponible", sizeof(data.status));
    if (data.count == 0 && TEC_STOP_COUNT)
        strlcpy(data.status, "Aucun bus prévu dans les deux prochaines heures", sizeof(data.status));
    tec_view_update(&data);
}

static void tec_task(void *arg)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    for (;;) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            struct view_data_tec_departures empty = {.status = "Wi-Fi non connecté"};
            utf8_copy(empty.stop_name, TEC_GENERATED_STOP_NAME, sizeof(empty.stop_name));
            tec_view_update(&empty);
        } else if (!CONFIG_TEC_API_KEY[0]) {
            struct view_data_tec_departures empty = {.status = "Saisissez la clé API avec idf.py menuconfig"};
            utf8_copy(empty.stop_name, TEC_GENERATED_STOP_NAME, sizeof(empty.stop_name));
            tec_view_update(&empty);
        } else if (time(NULL) < 1735689600) {
            struct view_data_tec_departures empty = {.status = "Synchronisation de l’heure…"};
            utf8_copy(empty.stop_name, TEC_GENERATED_STOP_NAME, sizeof(empty.stop_name));
            tec_view_update(&empty);
        } else {
            response_buffer_t body = {0};
            bool ok = download_updates(&body) == ESP_OK && parse_feed((uint8_t *)body.data, body.length);
            if (!ok) s_realtime_count = 0;
            refresh_screen(ok);
            heap_caps_free(body.data);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TEC_POLL_SECONDS * 1000));
    }
}

int indicator_tec_init(void)
{
    load_selected_stop();
    BaseType_t created = xTaskCreate(tec_task, "tec_poll", 8192, NULL, 4, &s_tec_task_handle);
    return created == pdPASS ? 0 : -1;
}
