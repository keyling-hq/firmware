#include <stdio.h>
#include <string.h>
#include "logbuf.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#define LOG_LINE_MAX    256

static uint8_t s_buf[LOGBUF_SIZE];
static uint32_t s_written;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_next;

static void append(const char *text, size_t len)
{
    taskENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < len; i++) {
        s_buf[(s_written + i) % LOGBUF_SIZE] = (uint8_t)text[i];
    }
    s_written += len;
    taskEXIT_CRITICAL(&s_lock);
}

static int log_vprintf(const char *fmt, va_list args)
{
    char line[LOG_LINE_MAX];
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (len > 0) {
        append(line, (size_t)len < sizeof(line) ? (size_t)len : sizeof(line) - 1);
    }
    return s_next ? s_next(fmt, args) : len;
}

esp_err_t logbuf_start(void)
{
    if (!s_next) {
        s_next = esp_log_set_vprintf(log_vprintf);
    }
    return ESP_OK;
}

size_t logbuf_read(uint32_t since, uint8_t *buf, size_t len, uint32_t *start, uint32_t *next)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t written = s_written;
    uint32_t oldest = written > LOGBUF_SIZE ? written - LOGBUF_SIZE : 0;
    uint32_t from = since < oldest || since > written ? oldest : since;
    size_t count = written - from < len ? written - from : len;
    for (size_t i = 0; i < count; i++) {
        buf[i] = s_buf[(from + i) % LOGBUF_SIZE];
    }
    taskEXIT_CRITICAL(&s_lock);
    *start = from;
    *next = from + count;
    return count;
}

uint32_t logbuf_end(void)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t written = s_written;
    taskEXIT_CRITICAL(&s_lock);
    return written;
}
