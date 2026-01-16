#include "wav_player.h"

#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <vector>

struct WavFmt {
    uint16_t audiofmt = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
};

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

static bool read_u16_le(FILE* f, uint16_t* out)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return false;
    *out = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

static bool read_u32_le(FILE* f, uint32_t* out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *out = (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    return true;
}

static bool parse_wav(FILE* f, WavFmt& fmt, uint32_t& data_size)
{
    char riff[4];
    char wave[4];
    uint32_t chunk_size = 0;
    if (fread(riff, 1, 4, f) != 4) return false;
    if (memcmp(riff, "RIFF", 4) != 0) return false;
    if (!read_u32_le(f, &chunk_size)) return false;
    if (fread(wave, 1, 4, f) != 4) return false;
    if (memcmp(wave, "WAVE", 4) != 0) return false;

    bool got_fmt = false;
    bool got_data = false;
    data_size = 0;

    while (!got_data) {
        char id[4];
        uint32_t size = 0;
        if (fread(id, 1, 4, f) != 4) return false;
        if (!read_u32_le(f, &size)) return false;

        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t audiofmt = 0;
            uint16_t channels = 0;
            uint32_t sample_rate = 0;
            uint32_t byte_rate = 0;
            uint16_t block_align = 0;
            uint16_t bits_per_sample = 0;

            if (!read_u16_le(f, &audiofmt)) return false;
            if (!read_u16_le(f, &channels)) return false;
            if (!read_u32_le(f, &sample_rate)) return false;
            if (!read_u32_le(f, &byte_rate)) return false;
            if (!read_u16_le(f, &block_align)) return false;
            if (!read_u16_le(f, &bits_per_sample)) return false;

            (void)byte_rate;
            (void)block_align;

            if (size > 16) {
                if (fseek(f, (long)(size - 16), SEEK_CUR) != 0) return false;
            }

            fmt.audiofmt = audiofmt;
            fmt.channels = channels;
            fmt.sample_rate = sample_rate;
            fmt.bits_per_sample = bits_per_sample;
            got_fmt = true;
            continue;
        }

        if (memcmp(id, "data", 4) == 0) {
            data_size = size;
            got_data = true;
            break;
        }

        if (fseek(f, (long)size, SEEK_CUR) != 0) return false;
    }

    if (!got_fmt || !got_data) return false;
    if (fmt.audiofmt != 1) return false;
    if (fmt.bits_per_sample != 8 && fmt.bits_per_sample != 16) return false;
    if (fmt.channels == 0 || fmt.channels > 2) return false;
    return true;
}

bool play_wav_file(const char* real_path)
{
    static const int kChannel = 0;
    if (!real_path || !*real_path) {
        return false;
    }

    FILE* f = fopen(real_path, "rb");
    if (!f) {
        return false;
    }
    setvbuf(f, nullptr, _IOFBF, 64 * 1024);

    WavFmt fmt;
    uint32_t data_size = 0;
    if (!parse_wav(f, fmt, data_size)) {
        fclose(f);
        return false;
    }

    const bool stereo = (fmt.channels == 2);
    const bool is_16 = (fmt.bits_per_sample == 16);
    const size_t kBufSize = 8192;
    static const size_t kBufCount = 3;
    static uint8_t buffers[kBufCount][kBufSize];

    wait_for_key_release(200);

    uint32_t remaining = data_size;
    size_t idx = 0;
    bool stopped = false;
    while (remaining > 0 && !stopped) {
        size_t len = remaining < kBufSize ? (size_t)remaining : kBufSize;
        size_t got = fread(buffers[idx], 1, len, f);
        if (got == 0) {
            break;
        }
        remaining -= (uint32_t)got;

        pump_input();
        if (any_key_pressed()) {
            stopped = true;
            break;
        }

        bool queued = false;
        while (!queued && !stopped) {
            if (is_16) {
                size_t samples = got / 2;
                queued = M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(buffers[idx]),
                    samples, fmt.sample_rate, stereo, 1, kChannel, false);
            } else {
                queued = M5.Speaker.playRaw(buffers[idx],
                    got, fmt.sample_rate, stereo, 1, kChannel, false);
            }
            if (!queued) {
                pump_input();
                if (any_key_pressed()) {
                    stopped = true;
                    break;
                }
                delay(1);
            }
        }

        idx = (idx + 1) % kBufCount;
    }

    fclose(f);
    if (stopped) {
        M5.Speaker.stop();
    }
    return true;
}
