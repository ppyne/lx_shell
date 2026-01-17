#include "lxsh_cli_bridge.h"

#include <stdlib.h>
#include <string.h>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ui/terminal.h"
#include "lx_runner.h"
#include "lxsh_runtime.h"

static QueueHandle_t g_lxsh_cli_queue = NULL;
static StaticQueue_t g_lxsh_cli_queue_buf;
static uint8_t g_lxsh_cli_queue_storage[64];

static void lxsh_cli_ensure_queue()
{
    if (g_lxsh_cli_queue) {
        return;
    }
    g_lxsh_cli_queue = xQueueCreateStatic(
        64,
        sizeof(uint8_t),
        g_lxsh_cli_queue_storage,
        &g_lxsh_cli_queue_buf);
}

void lxsh_cli_push_char(uint8_t c)
{
    lxsh_cli_ensure_queue();
    if (!g_lxsh_cli_queue) {
        return;
    }
    (void)xQueueSend(g_lxsh_cli_queue, &c, 0);
}

extern "C" int lxsh_cli_read_key(int* out_code)
{
    if (!out_code) {
        return 0;
    }
    lxsh_cli_ensure_queue();
    if (!g_lxsh_cli_queue) {
        return 0;
    }
    while (!lxsh_cancel_requested()) {
        uint8_t c = 0;
        if (xQueueReceive(g_lxsh_cli_queue, &c, pdMS_TO_TICKS(50)) == pdTRUE) {
            *out_code = (int)c;
            return 1;
        }
    }
    return 0;
}

extern "C" char* lxsh_cli_read_line(void)
{
    lxsh_cli_ensure_queue();
    if (!g_lxsh_cli_queue) {
        return NULL;
    }

    std::string line;
    term_raw_input_begin();

    while (!lxsh_cancel_requested()) {
        uint8_t c = 0;
        if (xQueueReceive(g_lxsh_cli_queue, &c, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            term_raw_input_end();
            term_putc('\n');
            break;
        }
        if (c == 0x08 || c == 0x7f) {
            if (!line.empty()) {
                line.pop_back();
                term_raw_input_backspace();
            }
            continue;
        }
        if (c < 0x20) {
            continue;
        }
        line.push_back((char)c);
        term_raw_input_char((char)c);
    }

    term_raw_input_end();

    if (lxsh_cancel_requested()) {
        return NULL;
    }

    char* out = (char*)malloc(line.size() + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, line.data(), line.size());
    out[line.size()] = '\0';
    return out;
}

extern "C" void lxsh_cli_prompt(const char* prompt)
{
    if (!prompt || !*prompt) {
        return;
    }
    term_puts(prompt);
}
