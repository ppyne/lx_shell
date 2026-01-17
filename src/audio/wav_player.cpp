#include "wav_player.h"

#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Arduino.h>

#include "audio_file_source_vfs.h"
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorWAV.h>

#include "audio_output_m5speaker.h"
#include "lx_runner.h"
#include "lxsh_exec_bridge.h"

static bool ctrl_c_requested()
{
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.ctrl) {
        return false;
    }
    for (char c : st.word) {
        if (c == 'c' || c == 'C') {
            if (lxsh_exec_is_active()) {
                lxsh_exec_request_cancel();
            }
            if (lx_script_is_active()) {
                lx_script_request_cancel();
            }
            return true;
        }
    }
    return false;
}

static bool any_key_pressed()
{
    if (ctrl_c_requested()) {
        return true;
    }
    if (lx_script_is_active()) {
        return false;
    }
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.word.empty()) return true;
    if (st.enter || st.del || st.tab) return true;
    if (st.fn || st.shift || st.ctrl || st.opt || st.alt) return true;
    return false;
}

static void adjust_volume(int delta)
{
    int vol = (int)M5.Speaker.getVolume();
    vol += delta;
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    M5.Speaker.setVolume((uint8_t)vol);
}

static void handle_volume_keys()
{
    if (lx_script_is_active()) {
        return;
    }
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.fn) {
        return;
    }

    for (char c : st.word) {
        if (c == ';' || c == ':') {
            adjust_volume(8);
        } else if (c == '.' || c == '>') {
            adjust_volume(-8);
        }
    }
}

static bool stop_requested()
{
    if (ctrl_c_requested()) {
        return true;
    }
    if (lxsh_exec_cancel_requested()) {
        return true;
    }
    if (lx_script_is_active()) {
        return false;
    }
    auto &st = M5Cardputer.Keyboard.keysState();
    if (st.word.empty() && !st.enter && !st.del && !st.tab) {
        return false;
    }

    if (st.fn) {
        bool non_volume = false;
        for (char c : st.word) {
            if (c == ';' || c == ':' || c == '.' || c == '>') {
                continue;
            }
            non_volume = true;
            break;
        }
        if (!non_volume && !st.enter && !st.del && !st.tab) {
            return false;
        }
    }

    return true;
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
        if (lxsh_exec_cancel_requested()) {
            break;
        }
        if (!any_key_pressed()) {
            break;
        }
        delay(5);
    }
}

bool play_wav_file(const char* real_path)
{
    if (!real_path || !*real_path) {
        return false;
    }

    AudioFileSourceVFS file;
    if (!file.open(real_path)) {
        return false;
    }

    static uint8_t io_buffer[32 * 1024];
    AudioFileSourceBuffer buffered(&file, io_buffer, sizeof(io_buffer));

    AudioGeneratorWAV wav;
    AudioOutputM5Speaker out(&M5.Speaker, 0);

    wait_for_key_release(200);
    if (lxsh_exec_cancel_requested()) {
        file.close();
        return true;
    }
    if (!wav.begin(&buffered, &out)) {
        file.close();
        return false;
    }

    bool stopped = false;
    while (wav.isRunning()) {
        pump_input();
        handle_volume_keys();
        if (stop_requested()) {
            stopped = true;
            wav.stop();
            break;
        }
        if (!wav.loop()) {
            wav.stop();
            break;
        }
        delay(1);
    }

    out.stop();
    buffered.close();
    file.close();
    if (stopped) {
        M5.Speaker.stop();
    }
    return true;
}
