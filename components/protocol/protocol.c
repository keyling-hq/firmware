#include <string.h>
#include "protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ota_update.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "typer.h"

static const char *TAG = "protocol";

#define RX_WINDOW           4096
#define CREDIT_BATCH        (RX_WINDOW / 4)
#define MAX_FRAME           bridge_Request_size
#define ENQUEUE_POLL_MS     20
#define RESTART_DELAY_MS    500

#define FLAG_RX             BIT0
#define FLAG_SESSION        BIT1
#define FLAG_ABORT          BIT2
#define FLAG_STATUS         BIT3
#define FLAG_ALL            (FLAG_RX | FLAG_SESSION | FLAG_ABORT | FLAG_STATUS)

_Static_assert(2 + MAX_FRAME <= RX_WINDOW, "a full frame must fit in the receive window");

static protocol_handlers_t s_handlers;
static void *s_ctx;
static EventGroupHandle_t s_flags;
static StreamBufferHandle_t s_rx;

static volatile bool s_session_requested;
static volatile bool s_accepting;
static bool s_session;

/* Owned by the protocol task */
static uint8_t s_frame[2 + MAX_FRAME];
static size_t s_have;
static size_t s_frame_len;
static size_t s_consumed;
static bridge_Request s_request;
static bool s_restart_pending;
static bool s_ota_reported_active;
static uint8_t s_ota_reported_percent;

static bool abort_pending(void)
{
    return xEventGroupGetBits(s_flags) & FLAG_ABORT;
}

/* ---- events -------------------------------------------------------------- */

static void send_event(const bridge_Event *event)
{
    static uint8_t buf[2 + bridge_Event_size];
    if (!s_session) {
        return;
    }
    pb_ostream_t os = pb_ostream_from_buffer(buf + 2, sizeof(buf) - 2);
    if (!pb_encode(&os, bridge_Event_fields, event)) {
        ESP_LOGE(TAG, "encode event %u: %s", (unsigned)event->which_msg, PB_GET_ERROR(&os));
        return;
    }
    buf[0] = os.bytes_written & 0xFF;
    buf[1] = os.bytes_written >> 8;
    if (s_handlers.send(buf, 2 + os.bytes_written, s_ctx) != ESP_OK) {
        ESP_LOGW(TAG, "event %u not sent", (unsigned)event->which_msg);
    }
}

static void send_credits(uint32_t bytes, bool reset)
{
    bridge_Event event = { .which_msg = bridge_Event_credits_tag };
    event.credits.bytes = bytes;
    event.credits.reset = reset;
    send_event(&event);
}

static void send_ack(uint32_t id, bridge_Result result)
{
    if (id == 0) {
        return;
    }
    bridge_Event event = { .which_msg = bridge_Event_ack_tag };
    event.ack.id = id;
    event.ack.result = result;
    send_event(&event);
}

static void send_status(void)
{
    bridge_Event event = { .which_msg = bridge_Event_status_tag };
    s_handlers.get_status(&event.status, s_ctx);
    send_event(&event);
}

static void send_config(void)
{
    bridge_Event event = { .which_msg = bridge_Event_config_tag };
    s_handlers.get_config(&event.config, s_ctx);
    send_event(&event);
}

static const char *const CAPABILITIES[] = {
    "text", "strokes", "key_combo", "consumer", "keyboard_report", "config", "ota",
};

static void send_hello(void)
{
    bridge_Event event = { .which_msg = bridge_Event_hello_tag };
    event.hello.protocol_version = PROTOCOL_VERSION;
    event.hello.max_frame = MAX_FRAME;
    strlcpy(event.hello.firmware_version, esp_app_get_description()->version,
            sizeof(event.hello.firmware_version));
    _Static_assert(sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]) <=
                   sizeof(event.hello.capabilities) / sizeof(event.hello.capabilities[0]), "capabilities");
    for (size_t i = 0; i < sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]); i++) {
        strlcpy(event.hello.capabilities[i], CAPABILITIES[i], sizeof(event.hello.capabilities[i]));
    }
    event.hello.capabilities_count = sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]);
    send_event(&event);
}

/* ---- requests ------------------------------------------------------------ */

/* Retries while the typing queue is full, giving up only if an abort arrives meanwhile. */
#define ENQUEUE(call) ({                                                    \
    esp_err_t _err;                                                         \
    while ((_err = (call)) == ESP_ERR_TIMEOUT && !abort_pending()) {        \
    }                                                                       \
    _err == ESP_OK ? bridge_Result_RESULT_OK                                \
    : _err == ESP_ERR_TIMEOUT ? bridge_Result_RESULT_ABORTED                \
    : _err == ESP_ERR_INVALID_ARG ? bridge_Result_RESULT_INVALID            \
    : bridge_Result_RESULT_FAILED;                                          \
})

static bridge_Result handle_text(const char *text)
{
    size_t len = strlen(text);
    while (len) {
        size_t chunk = len < TYPER_TEXT_CHUNK ? len : TYPER_TEXT_CHUNK;
        bridge_Result result = ENQUEUE(typer_text(text, chunk, pdMS_TO_TICKS(ENQUEUE_POLL_MS)));
        if (result != bridge_Result_RESULT_OK) {
            return result;
        }
        text += chunk;
        len -= chunk;
    }
    return bridge_Result_RESULT_OK;
}

static bridge_Result handle_strokes(const uint8_t *strokes, size_t len)
{
    if (len % TYPER_STROKE_SIZE) {
        return bridge_Result_RESULT_INVALID;
    }
    while (len) {
        size_t chunk = len < TYPER_STROKES_CHUNK ? len : TYPER_STROKES_CHUNK;
        bridge_Result result = ENQUEUE(typer_strokes(strokes, chunk, pdMS_TO_TICKS(ENQUEUE_POLL_MS)));
        if (result != bridge_Result_RESULT_OK) {
            return result;
        }
        strokes += chunk;
        len -= chunk;
    }
    return bridge_Result_RESULT_OK;
}

static void report_ota(bool active)
{
    uint8_t percent = ota_update_progress();
    if (active != s_ota_reported_active || percent != s_ota_reported_percent) {
        s_ota_reported_active = active;
        s_ota_reported_percent = percent;
        s_handlers.ota_state(active, percent, s_ctx);
    }
}

static void abandon_ota(void)
{
    if (ota_update_active()) {
        ota_update_abort();
        report_ota(false);
    }
}

static bridge_Result ota_result(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return bridge_Result_RESULT_OK;
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_STATE:
        return bridge_Result_RESULT_INVALID;
    default:
        return bridge_Result_RESULT_FAILED;
    }
}

static bridge_Result handle_ota(const bridge_Request *req)
{
    esp_err_t err;
    switch (req->which_msg) {
    case bridge_Request_ota_begin_tag:
        report_ota(true);
        err = ota_update_begin(req->ota_begin.size, req->ota_begin.sha256.bytes, req->ota_begin.sha256.size);
        report_ota(err == ESP_OK);
        return ota_result(err);
    case bridge_Request_ota_data_tag:
        err = ota_update_write(req->ota_data.offset, req->ota_data.data.bytes, req->ota_data.data.size);
        if (err != ESP_OK) {
            abandon_ota();
        } else {
            report_ota(true);
        }
        return ota_result(err);
    case bridge_Request_ota_end_tag:
        err = ota_update_end();
        report_ota(false);
        s_restart_pending = err == ESP_OK;
        return ota_result(err);
    default:
        abandon_ota();
        return bridge_Result_RESULT_OK;
    }
}

static bridge_Result handle_request(const bridge_Request *req)
{
    const TickType_t poll = pdMS_TO_TICKS(ENQUEUE_POLL_MS);

    bool input = req->which_msg >= bridge_Request_text_tag && req->which_msg < bridge_Request_ping_tag;
    if (input && ota_update_active()) {
        return bridge_Result_RESULT_FAILED;
    }

    switch (req->which_msg) {
    case bridge_Request_ota_begin_tag:
    case bridge_Request_ota_data_tag:
    case bridge_Request_ota_end_tag:
    case bridge_Request_ota_abort_tag:
        return handle_ota(req);
    case bridge_Request_text_tag:
        return handle_text(req->text.text);
    case bridge_Request_strokes_tag:
        return handle_strokes(req->strokes.strokes.bytes, req->strokes.strokes.size);
    case bridge_Request_key_combo_tag:
        return ENQUEUE(typer_combo(req->key_combo.modifiers, req->key_combo.keys.bytes,
                                   req->key_combo.keys.size, poll));
    case bridge_Request_consumer_tag:
        return ENQUEUE(typer_consumer(req->consumer.usage, poll));
    case bridge_Request_keyboard_report_tag:
        return ENQUEUE(typer_keyboard_report(req->keyboard_report.modifiers, req->keyboard_report.keys.bytes,
                                             req->keyboard_report.keys.size, poll));
    case bridge_Request_get_config_tag:
        send_config();
        return bridge_Result_RESULT_OK;
    case bridge_Request_set_config_tag: {
        bridge_Result result = s_handlers.set_config(&req->set_config.config, s_ctx);
        send_config();
        return result;
    }
    case bridge_Request_ping_tag:
        return bridge_Result_RESULT_OK;
    default:
        return bridge_Result_RESULT_INVALID;
    }
}

static void handle_frame(const uint8_t *payload, size_t len)
{
    memset(&s_request, 0, sizeof(s_request));
    pb_istream_t is = pb_istream_from_buffer(payload, len);
    if (!pb_decode(&is, bridge_Request_fields, &s_request)) {
        ESP_LOGW(TAG, "bad request: %s", PB_GET_ERROR(&is));
        send_ack(s_request.id, bridge_Result_RESULT_INVALID);
        return;
    }
    send_ack(s_request.id, handle_request(&s_request));

    if (s_restart_pending) {
        ESP_LOGI(TAG, "restarting into the new firmware");
        vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
        esp_restart();
    }
}

/* ---- streams ------------------------------------------------------------- */

static void reset_rx(void)
{
    xStreamBufferReset(s_rx);
    s_have = 0;
    s_frame_len = 0;
    s_consumed = 0;
}

static void return_credits(void)
{
    if (s_consumed) {
        send_credits(s_consumed, false);
        s_consumed = 0;
    }
}

/* Processes complete frames until the stream is empty or an abort is requested. */
static void process_rx(void)
{
    while (!abort_pending()) {
        size_t want = s_have < 2 ? 2 - s_have : 2 + s_frame_len - s_have;
        size_t n = xStreamBufferReceive(s_rx, s_frame + s_have, want, 0);
        if (n == 0) {
            break;
        }
        s_have += n;
        if (s_have == 2) {
            s_frame_len = s_frame[0] | (s_frame[1] << 8);
            if (s_frame_len > MAX_FRAME) {
                ESP_LOGW(TAG, "frame of %u bytes exceeds %u, resetting stream",
                         (unsigned)s_frame_len, (unsigned)MAX_FRAME);
                protocol_abort();
                return;
            }
        }
        if (s_have == 2 + s_frame_len) {
            handle_frame(s_frame + 2, s_frame_len);
            s_consumed += s_have;
            s_have = 0;
            if (s_consumed >= CREDIT_BATCH) {
                return_credits();
            }
        }
    }
    /* After an abort the phone waits for a credit reset; an increment now would be misleading. */
    if (!abort_pending()) {
        return_credits();
    }
}

static void handle_abort(void)
{
    abandon_ota();
    typer_abort();
    reset_rx();
    if (s_session) {
        s_accepting = true;
        send_credits(RX_WINDOW, true);
    }
    ESP_LOGI(TAG, "aborted");
}

static void handle_session(void)
{
    bool active = s_session_requested;
    if (active == s_session) {
        return;
    }
    s_session = active;
    s_accepting = false;
    abandon_ota();
    /*
     * Frames the phone already sent are still typed after a disconnect: only a new session
     * drops what is left, which by then can only be an incomplete frame.
     */
    if (active) {
        reset_rx();
        s_accepting = true;
        send_hello();
        send_credits(RX_WINDOW, true);
        send_status();
    }
}

static void protocol_task(void *arg)
{
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_flags, FLAG_ALL, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & FLAG_SESSION) {
            handle_session();
        }
        if (bits & FLAG_ABORT) {
            handle_abort();
        }
        if ((bits & FLAG_STATUS) && s_session) {
            send_status();
        }
        process_rx();
    }
}

/* ---- public -------------------------------------------------------------- */

esp_err_t protocol_start(const protocol_handlers_t *handlers, void *ctx)
{
    s_handlers = *handlers;
    s_ctx = ctx;
    s_flags = xEventGroupCreate();
    s_rx = xStreamBufferCreate(RX_WINDOW, 1);
    ESP_RETURN_ON_FALSE(s_flags && s_rx, ESP_ERR_NO_MEM, TAG, "alloc");
    BaseType_t ok = xTaskCreate(protocol_task, "protocol", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void protocol_session(bool active)
{
    s_session_requested = active;
    if (!active) {
        s_accepting = false;
    }
    xEventGroupSetBits(s_flags, FLAG_SESSION);
}

void protocol_rx(const uint8_t *data, size_t len)
{
    if (!s_accepting) {
        return;
    }
    if (xStreamBufferSend(s_rx, data, len, 0) != len) {
        ESP_LOGW(TAG, "phone exceeded its credits, resetting stream");
        protocol_abort();
        return;
    }
    xEventGroupSetBits(s_flags, FLAG_RX);
}

void protocol_abort(void)
{
    s_accepting = false;
    typer_abort();
    xEventGroupSetBits(s_flags, FLAG_ABORT);
}

void protocol_status_changed(void)
{
    xEventGroupSetBits(s_flags, FLAG_STATUS);
}
