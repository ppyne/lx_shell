#include "settings.h"

#include "fs/fs.h"
#include "lx_runner.h"

#include <string>
#include <ctype.h>

namespace {
static uint8_t pref_brightness = 255;
static uint32_t pref_saver_start_ms = 2 * 60 * 1000UL;
static uint32_t pref_screen_off_ms = 5 * 60 * 1000UL;
static std::string pref_lx_profile = "power";
static const char* pref_path = "/media/0/.lxshellrc";
static const char* pref_script_path = "/media/0/.lxscriptrc";

static std::string trim_copy(const std::string& in)
{
    size_t start = 0;
    while (start < in.size() && isspace((unsigned char)in[start])) {
        start++;
    }
    size_t end = in.size();
    while (end > start && isspace((unsigned char)in[end - 1])) {
        end--;
    }
    return in.substr(start, end - start);
}

static bool parse_uint(const std::string& s, uint32_t& out)
{
    if (s.empty()) {
        return false;
    }
    uint32_t val = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        val = val * 10 + (uint32_t)(c - '0');
    }
    out = val;
    return true;
}
} // namespace

void settings_init()
{
    settings_load_if_available();
    settings_load_script_if_available();
}

void settings_load_if_available()
{
    if (!fs_sd_mounted()) {
        return;
    }
    std::string content;
    if (!fs_read_file(pref_path, content)) {
        return;
    }
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = trim_copy(content.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim_copy(line.substr(0, eq));
        std::string val = trim_copy(line.substr(eq + 1));
        uint32_t num = 0;
        if (!parse_uint(val, num)) {
            continue;
        }
        if (key == "brightness") {
            if (num < 7) num = 7;
            if (num > 255) num = 255;
            pref_brightness = (uint8_t)num;
        } else if (key == "screensaver_minutes") {
            if (num < 1) num = 1;
            if (num > 120) num = 120;
            pref_saver_start_ms = num * 60 * 1000UL;
        } else if (key == "screen_off_minutes") {
            if (num < 1) num = 1;
            if (num > 120) num = 120;
            pref_screen_off_ms = num * 60 * 1000UL;
        }
    }
}

void settings_save_if_available()
{
    if (!fs_sd_mounted()) {
        return;
    }
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "brightness=%u\nscreensaver_minutes=%lu\nscreen_off_minutes=%lu\n",
        (unsigned)pref_brightness,
        (unsigned long)(pref_saver_start_ms / 60000UL),
        (unsigned long)(pref_screen_off_ms / 60000UL));
    if (n <= 0) {
        return;
    }
    fs_write_file(pref_path, reinterpret_cast<const unsigned char*>(buf), (size_t)n);
}

void settings_load_script_if_available()
{
    if (!fs_sd_mounted()) {
        return;
    }
    std::string content;
    if (!fs_read_file(pref_script_path, content)) {
        return;
    }
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = trim_copy(content.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim_copy(line.substr(0, eq));
        std::string val = trim_copy(line.substr(eq + 1));
        if (key == "profile") {
            if (settings_set_lx_profile(val.c_str())) {
                lx_set_profile(val.c_str());
            }
        }
    }
}

void settings_save_script_if_available()
{
    if (!fs_sd_mounted()) {
        return;
    }
    std::string content = "profile=" + pref_lx_profile + "\n";
    fs_write_file(pref_script_path,
        reinterpret_cast<const unsigned char*>(content.c_str()),
        content.size());
}

uint8_t settings_get_brightness()
{
    return pref_brightness;
}

void settings_set_brightness(uint8_t value)
{
    if (value < 7) value = 7;
    pref_brightness = value;
}

uint32_t settings_get_saver_start_ms()
{
    return pref_saver_start_ms;
}

uint32_t settings_get_screen_off_ms()
{
    return pref_screen_off_ms;
}

void settings_set_saver_minutes(uint32_t minutes)
{
    if (minutes < 1) minutes = 1;
    if (minutes > 120) minutes = 120;
    pref_saver_start_ms = minutes * 60 * 1000UL;
}

void settings_set_screen_off_minutes(uint32_t minutes)
{
    if (minutes < 1) minutes = 1;
    if (minutes > 120) minutes = 120;
    pref_screen_off_ms = minutes * 60 * 1000UL;
}

const char* settings_get_lx_profile()
{
    return pref_lx_profile.c_str();
}

bool settings_set_lx_profile(const char* name)
{
    if (!name || !*name) {
        return false;
    }
    std::string val = name;
    if (val != "safe" && val != "balanced" && val != "power") {
        return false;
    }
    pref_lx_profile = val;
    return true;
}
