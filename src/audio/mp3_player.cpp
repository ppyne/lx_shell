#include "mp3_player.h"

#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <deque>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static bool any_key_pressed()
{
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.word.empty()) return true;
    if (st.enter || st.del || st.tab) return true;
    if (st.fn || st.shift || st.ctrl || st.opt || st.alt) return true;
    return false;
}

static void pump_input()
{
    M5.update();
    M5Cardputer.update();
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();
}

static void wait_for_key_release(uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        pump_input();
        if (!any_key_pressed()) {
            break;
        }
        delay(5);
    }
}

struct Mp3Buffer {
    std::vector<int16_t> pcm;
    size_t samples = 0;
    int hz = 0;
    bool stereo = false;
    uint32_t duration_ms = 0;
};

struct Mp3Context {
    FILE* f = nullptr;
    mp3dec_t dec{};
    std::vector<uint8_t> buf;
    size_t bytes = 0;
    bool eof = false;
    volatile bool stop = false;
    volatile bool done = false;
    volatile bool task_done = false;
    QueueHandle_t free_q = nullptr;
    QueueHandle_t ready_q = nullptr;
    Mp3Buffer* buffers = nullptr;
};

static void mp3_decode_task(void* arg)
{
    Mp3Context* ctx = static_cast<Mp3Context*>(arg);
    const size_t buf_size = ctx->buf.size();

    while (!ctx->stop && !ctx->done) {
        int idx = -1;
        if (xQueueReceive(ctx->free_q, &idx, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if (ctx->stop) {
            break;
        }

        while (!ctx->stop) {
            if (!ctx->eof && ctx->bytes < buf_size / 2) {
                size_t n = fread(ctx->buf.data() + ctx->bytes, 1, buf_size - ctx->bytes, ctx->f);
                if (n == 0) {
                    ctx->eof = true;
                } else {
                    ctx->bytes += n;
                }
            }

            if (ctx->bytes == 0) {
                if (ctx->eof) {
                    ctx->done = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&ctx->dec, ctx->buf.data(), (int)ctx->bytes,
                ctx->buffers[idx].pcm.data(), &info);

            if (info.frame_bytes == 0) {
                if (ctx->eof) {
                    ctx->done = true;
                    break;
                }
                if (ctx->bytes > 0) {
                    memmove(ctx->buf.data(), ctx->buf.data() + 1, ctx->bytes - 1);
                    ctx->bytes -= 1;
                }
                continue;
            }

            if ((size_t)info.frame_bytes <= ctx->bytes) {
                memmove(ctx->buf.data(), ctx->buf.data() + info.frame_bytes, ctx->bytes - info.frame_bytes);
                ctx->bytes -= info.frame_bytes;
            } else {
                ctx->bytes = 0;
            }

            if (samples <= 0 || info.hz == 0) {
                continue;
            }

            Mp3Buffer& out = ctx->buffers[idx];
            out.stereo = (info.channels == 2);
            out.hz = info.hz;
            out.samples = (size_t)samples * (out.stereo ? 2 : 1);
            out.duration_ms = (uint32_t)((samples * 1000L) / (long)info.hz);
            if (out.duration_ms < 10) out.duration_ms = 10;

            xQueueSend(ctx->ready_q, &idx, portMAX_DELAY);
            break;
        }
    }

    ctx->task_done = true;
    vTaskDelete(nullptr);
}

bool play_mp3_file(const char* real_path)
{
    static const int kChannel = 0;
    if (!real_path || !*real_path) {
        return false;
    }

    FILE* f = fopen(real_path, "rb");
    if (!f) {
        return false;
    }

    Mp3Context ctx;
    ctx.f = f;
    mp3dec_init(&ctx.dec);
    ctx.buf.assign(48 * 1024, 0);

    static const size_t kPcmBuffers = 24;
    std::vector<Mp3Buffer> buffers(kPcmBuffers);
    for (size_t i = 0; i < kPcmBuffers; i++) {
        buffers[i].pcm.assign(MINIMP3_MAX_SAMPLES_PER_FRAME, 0);
    }
    ctx.buffers = buffers.data();

    ctx.free_q = xQueueCreate(kPcmBuffers, sizeof(int));
    ctx.ready_q = xQueueCreate(kPcmBuffers, sizeof(int));
    if (!ctx.free_q || !ctx.ready_q) {
        fclose(f);
        return false;
    }

    for (int i = 0; i < (int)kPcmBuffers; i++) {
        xQueueSend(ctx.free_q, &i, 0);
    }

    TaskHandle_t decode_task = nullptr;
    xTaskCreatePinnedToCore(mp3_decode_task, "mp3_decode", 32768, &ctx, 2, &decode_task, 0);
    (void)decode_task;

    wait_for_key_release(200);

    const size_t kPrefill = 8;
    while (uxQueueMessagesWaiting(ctx.ready_q) < kPrefill && !ctx.done) {
        pump_input();
        if (any_key_pressed()) {
            ctx.stop = true;
            break;
        }
        delay(5);
    }

    bool stopped = false;
    std::deque<int> inflight;
    while (!stopped) {
        pump_input();
        if (any_key_pressed()) {
            stopped = true;
            ctx.stop = true;
            break;
        }

        int playing = (int)M5.Speaker.isPlaying(kChannel);
        while ((int)inflight.size() > playing) {
            int done_idx = inflight.front();
            inflight.pop_front();
            xQueueSend(ctx.free_q, &done_idx, 0);
        }

        while (playing < 2) {
            int idx = -1;
            if (xQueueReceive(ctx.ready_q, &idx, 0) != pdTRUE) {
                break;
            }
            Mp3Buffer& chunk = buffers[(size_t)idx];
            if (chunk.samples == 0 || chunk.hz == 0) {
                xQueueSend(ctx.free_q, &idx, 0);
                continue;
            }

            M5.Speaker.playRaw(chunk.pcm.data(), chunk.samples, chunk.hz,
                chunk.stereo, 1, kChannel, false);
            inflight.push_back(idx);
            playing = (int)M5.Speaker.isPlaying(kChannel);
            if (playing < (int)inflight.size()) {
                playing = (int)inflight.size();
            }
        }

        if (ctx.done && uxQueueMessagesWaiting(ctx.ready_q) == 0 && inflight.empty()) {
            break;
        }

        delay(1);
    }

    ctx.stop = true;
    uint32_t wait_start = millis();
    while (!ctx.task_done && (millis() - wait_start) < 500) {
        delay(1);
    }

    if (ctx.free_q) vQueueDelete(ctx.free_q);
    if (ctx.ready_q) vQueueDelete(ctx.ready_q);

    fclose(f);
    if (stopped) {
        M5.Speaker.stop();
    }
    return true;
}
