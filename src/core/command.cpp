#include "command.h"

#include "ui/terminal.h"
#include "ui/encoding.h"
#include "ui/screen.h"
#include "ui/screensaver.h"
#include "fs/fs.h"
#include "editor/editor.h"
#include "lx_runner.h"
#include "audio/mp3_player.h"
#include "audio/wav_player.h"

#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include <Arduino.h>
#include <M5Unified.h>
#include <M5Cardputer.h>

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

static const char* find_unquoted_char(const char* line, char ch)
{
    if (!line) {
        return nullptr;
    }
    bool in_quote = false;
    for (const char* p = line; *p; ++p) {
        if (*p == '"' && (p == line || p[-1] != '\\')) {
            in_quote = !in_quote;
            continue;
        }
        if (!in_quote && *p == ch) {
            return p;
        }
    }
    return nullptr;
}

static bool view_any_key_pressed()
{
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.word.empty()) return true;
    if (st.enter || st.del || st.tab) return true;
    if (st.fn || st.shift || st.ctrl || st.opt || st.alt) return true;
    return false;
}

static bool view_any_key_pressed_no_fn()
{
    auto &st = M5Cardputer.Keyboard.keysState();
    if (!st.word.empty()) return true;
    if (st.enter || st.del || st.tab) return true;
    if (st.shift || st.ctrl || st.opt || st.alt) return true;
    return false;
}

static void view_wait_for_key()
{
    // Wait for any prior key to be released first.
    for (;;) {
        M5.update();
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
        if (!view_any_key_pressed()) {
            break;
        }
        delay(10);
    }

    // Then wait for a fresh key press.
    for (;;) {
        M5.update();
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
        if (view_any_key_pressed()) {
            break;
        }
        delay(10);
    }
}

static void view_wait_for_key_release()
{
    for (;;) {
        M5.update();
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
        if (!view_any_key_pressed()) {
            break;
        }
        delay(10);
    }
}

static void view_wait_for_nav_release()
{
    for (;;) {
        M5.update();
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
        auto &st = M5Cardputer.Keyboard.keysState();
        bool nav_pressed = false;
        for (char c : st.word) {
            if (c == '/' || c == '?' || c == ',' || c == '<') {
                nav_pressed = true;
                break;
            }
        }
        if (!nav_pressed) {
            break;
        }
        delay(10);
    }
}

static void view_wait_for_char_release(char target)
{
    for (;;) {
        M5.update();
        M5Cardputer.update();
        M5Cardputer.Keyboard.updateKeyList();
        M5Cardputer.Keyboard.updateKeysState();
        auto &st = M5Cardputer.Keyboard.keysState();
        bool pressed = false;
        for (char c : st.word) {
            if (c == target) {
                pressed = true;
                break;
            }
        }
        if (!pressed) {
            break;
        }
        delay(10);
    }
}

static bool has_ext(const char* path, const char* ext);

static bool view_render_image(const char* real_path)
{
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    M5.Display.setTextDatum(lgfx::datum_t::middle_center);
    const bool is_png = has_ext(real_path, ".png");
    const bool is_jpg = has_ext(real_path, ".jpg") || has_ext(real_path, ".jpeg");
    bool ok = is_png
        ? M5.Display.drawPngFile(real_path, 0, 0, w, h, 0, 0, 0.0f, 0.0f,
            lgfx::datum_t::middle_center)
        : M5.Display.drawJpgFile(real_path, 0, 0, w, h, 0, 0, 0.0f, 0.0f,
            lgfx::datum_t::middle_center);
    M5.Display.setTextDatum(lgfx::datum_t::top_left);
    return ok;
}

static int slideshow_read_input()
{
    M5.update();
    M5Cardputer.update();
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();

    auto &st = M5Cardputer.Keyboard.keysState();
    if (st.fn) {
        for (char c : st.word) {
            if (c == '/' || c == '?') return 1;
            if (c == ',' || c == '<') return -1;
        }
    }

    for (char c : st.word) {
        if (c == 'r' || c == 'R') return 2;
    }

    if (!st.word.empty() || st.enter || st.del || st.tab || st.shift || st.ctrl || st.opt || st.alt) {
        return 99;
    }

    return 0;
}

static bool has_ext(const char* path, const char* ext)
{
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }
    const char* tail = path + (path_len - ext_len);
    for (size_t i = 0; i < ext_len; i++) {
        char a = (char)tolower((unsigned char)tail[i]);
        char b = (char)tolower((unsigned char)ext[i]);
        if (a != b) {
            return false;
        }
    }
    return true;
}


static const float kBatteryCapacityMah = 1200.0f;
static const float kEstimatedDischargeMa = 200.0f;
static const float kEstimatedChargeMa = 500.0f;

static int estimate_minutes_remaining(int percent)
{
    if (percent <= 0 || kEstimatedDischargeMa <= 0.0f) {
        return 0;
    }
    float remaining_mah = kBatteryCapacityMah * (percent / 100.0f);
    float hours = remaining_mah / kEstimatedDischargeMa;
    int minutes = (int)(hours * 60.0f + 0.5f);
    return minutes;
}

static int estimate_minutes_to_full(int percent)
{
    if (percent >= 100 || kEstimatedChargeMa <= 0.0f) {
        return 0;
    }
    float remaining_mah = kBatteryCapacityMah * ((100.0f - percent) / 100.0f);
    float hours = remaining_mah / kEstimatedChargeMa;
    int minutes = (int)(hours * 60.0f + 0.5f);
    return minutes;
}

static std::string man_entry(const char* name)
{
    struct ManItem {
        const char* name;
        const char* text;
    };

    static const ManItem items[] = {
        {"ls",
         "NAME\n"
         "  ls - list directory contents\n"
         "\n"
         "SYNOPSIS\n"
         "  ls [options] [path]\n"
         "\n"
         "OPTIONS\n"
         "  -a   include hidden entries\n"
         "  -l   long format\n"
         "  -t   sort by time\n"
         "  -r   reverse sort\n"
         "\n"
         "LONG FORMAT\n"
         "  Example: ls -la\n"
         "  drw-s-  512 2024-01-01 /media/0\n"
         "  -rwh-- 1024 2024-01-01 hello.txt\n"
         "\n"
         "  d / -  directory or file\n"
         "  r/w    readable / writable\n"
         "  h      hidden by name (starts with '.')\n"
         "  H      hidden by FAT attribute\n"
         "  s      system flag (FAT32; virtual entries)\n"
         "  a      archive flag (FAT32)\n"
         "  size   file size (bytes have no suffix, otherwise K/M/G)\n"
         "  date   file date\n"},
        {"pwd",
         "NAME\n"
         "  pwd - print working directory\n"
         "\n"
         "SYNOPSIS\n"
         "  pwd\n"},
        {"cd",
         "NAME\n"
         "  cd - change directory\n"
         "\n"
         "SYNOPSIS\n"
         "  cd [path]\n"},
        {"mount",
         "NAME\n"
         "  mount - mount SD card\n"
         "\n"
         "SYNOPSIS\n"
         "  mount\n"},
        {"umount",
         "NAME\n"
         "  umount - unmount SD card\n"
         "\n"
         "SYNOPSIS\n"
         "  umount\n"},
        {"mkdir",
         "NAME\n"
         "  mkdir - create directory\n"
         "\n"
         "SYNOPSIS\n"
         "  mkdir <path>\n"},
        {"rmdir",
         "NAME\n"
         "  rmdir - remove directory\n"
         "\n"
         "SYNOPSIS\n"
         "  rmdir <path>\n"},
        {"cp",
         "NAME\n"
         "  cp - copy file\n"
         "\n"
         "SYNOPSIS\n"
         "  cp <src> <dst>\n"},
        {"mv",
         "NAME\n"
         "  mv - move/rename file\n"
         "\n"
         "SYNOPSIS\n"
         "  mv <src> <dst>\n"},
        {"rm",
         "NAME\n"
         "  rm - remove file (asks confirmation)\n"
         "\n"
         "SYNOPSIS\n"
         "  rm <path>\n"},
        {"touch",
         "NAME\n"
         "  touch - create empty file or update timestamp\n"
         "\n"
         "SYNOPSIS\n"
         "  touch <path>\n"},
        {"cat",
         "NAME\n"
         "  cat - print file contents\n"
         "\n"
         "SYNOPSIS\n"
         "  cat <path>\n"},
        {"lx",
         "NAME\n"
         "  lx - run a .lx script\n"
         "\n"
         "SYNOPSIS\n"
         "  lx <path>\n"
         "  lx --profile <safe|balanced|power> <path>\n"
         "  lx -p <safe|balanced|power> <path>\n"
         "\n"
         "PROFILES\n"
         "  safe     - highest reserve, most conservative\n"
         "  balanced - default reserve, good for most scripts\n"
         "  power    - lowest reserve, maximum capacity (higher OOM risk)\n"
         "\n"
         "NOTES\n"
         "  The default profile lives in RAM only and resets to balanced on reboot.\n"
         "  Using --profile changes the profile for that run only.\n"},
        {"lxprofile",
         "NAME\n"
         "  lxprofile - set default Lx execution profile\n"
         "\n"
         "SYNOPSIS\n"
         "  lxprofile [safe|balanced|power]\n"
         "\n"
         "PROFILES\n"
         "  safe     - highest reserve, most conservative\n"
         "  balanced - default reserve, good for most scripts\n"
         "  power    - lowest reserve, maximum capacity (higher OOM risk)\n"
         "\n"
         "NOTES\n"
         "  The default profile lives in RAM only and resets to balanced on reboot.\n"},
        {"more",
         "NAME\n"
         "  more - page through text\n"
         "\n"
         "SYNOPSIS\n"
         "  more <path>\n"
         "  <cmd> | more\n"},
        {"less",
         "NAME\n"
         "  less - alias for more\n"
         "\n"
         "SYNOPSIS\n"
         "  less <path>\n"},
        {"tee",
         "NAME\n"
         "  tee - write piped output to a file\n"
         "\n"
         "SYNOPSIS\n"
         "  <cmd> | tee [-a] <path>\n"
         "\n"
         "OPTIONS\n"
         "  -a   append instead of overwrite\n"},
        {"find",
         "NAME\n"
         "  find - search files\n"
         "\n"
         "SYNOPSIS\n"
         "  find <path>\n"
         "  find <path> -name <pattern>\n"
         "  find <path> -iname <pattern>\n"},
        {"vi",
         "NAME\n"
         "  vi - minimal editor\n"
         "\n"
         "SYNOPSIS\n"
         "  vi [path]\n"
         "\n"
         "NOTES\n"
         "  Modes: NORMAL/INSERT/COMMAND/Search\n"},
        {"view",
         "NAME\n"
         "  view - display a PNG or JPEG image\n"
         "\n"
         "SYNOPSIS\n"
         "  view <path>\n"
         "\n"
         "NOTES\n"
         "  The image is scaled to fit the screen while preserving aspect ratio.\n"
         "  Press 'r' to rotate 90 degrees.\n"
         "  Press any other key to return to the shell.\n"},
        {"slideshow",
         "NAME\n"
         "  slideshow - browse images in a folder\n"
         "\n"
         "SYNOPSIS\n"
         "  slideshow [-t seconds] <path>\n"
         "\n"
         "NOTES\n"
         "  Use fn+left / fn+right to navigate.\n"
         "  Press 'r' to rotate 90 degrees (disabled with -t).\n"
         "  The -t value is clamped to 2..120 seconds.\n"
         "  Press any key to return to the shell.\n"},
        {"play",
         "NAME\n"
         "  play - play a WAV or MP3 file\n"
         "\n"
         "SYNOPSIS\n"
         "  play [-v 0-100] <path>\n"
         "\n"
         "NOTES\n"
         "  Press any key to stop playback.\n"},
        {"nano",
         "NAME\n"
         "  nano - minimal editor (nano-style)\n"
         "\n"
         "SYNOPSIS\n"
         "  nano [path]\n"
         "\n"
         "KEYS\n"
         "  ^O write  ^X exit  ^W search  ^K cut line  ^U paste\n"},
        {"uptime",
         "NAME\n"
         "  uptime - show time since boot\n"
         "\n"
         "SYNOPSIS\n"
         "  uptime\n"},
        {"battery",
         "NAME\n"
         "  battery - show battery status\n"
         "\n"
         "SYNOPSIS\n"
         "  battery\n"
         "\n"
         "NOTES\n"
         "  Remaining time is an estimate based on fixed defaults.\n"},
        {"clear",
         "NAME\n"
         "  clear - clear the screen\n"
         "\n"
         "SYNOPSIS\n"
         "  clear\n"
         "  reset\n"},
        {"shutdown",
         "NAME\n"
         "  shutdown - halt or restart now\n"
         "\n"
         "SYNOPSIS\n"
         "  shutdown -h\n"
         "  shutdown -r\n"},
        {"man",
         "NAME\n"
         "  man - show command help\n"
         "\n"
         "SYNOPSIS\n"
         "  man <command>\n"}
    };

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        if (strcmp(items[i].name, name) == 0) {
            return items[i].text;
        }
    }

    return "";
}

static std::string to_cp437(const std::string& utf8)
{
    std::vector<char> buf(utf8.size() + 1);
    size_t len = utf8_to_cp437(utf8.c_str(), buf.data(), buf.size());
    return std::string(buf.data(), len);
}

// ------------------------------------------------------------
// Parsing simple : commande + deux arguments max
// ------------------------------------------------------------

static const char* parse_token(const char* line, char* out)
{
    out[0] = 0;

    while (*line == ' ') line++;
    if (!*line) {
        return line;
    }

    if (*line == '"') {
        line++;
        while (*line) {
            if (*line == '\\') {
                line++;
                if (*line == '"' || *line == '\\') {
                    *out++ = *line++;
                } else if (*line) {
                    *out++ = '\\';
                }
            } else if (*line == '"') {
                break;
            } else {
                *out++ = *line++;
            }
        }
        if (*line == '"') line++;
        *out = 0;
        return line;
    }

    while (*line && *line != ' ') {
        *out++ = *line++;
    }
    *out = 0;
    return line;
}

static void parse_line(const char* line, char* cmd, char* arg1, char* arg2, char* arg3)
{
    cmd[0] = 0;
    arg1[0] = 0;
    arg2[0] = 0;
    arg3[0] = 0;

    const char* p = line;
    p = parse_token(p, cmd);
    p = parse_token(p, arg1);
    p = parse_token(p, arg2);
    parse_token(p, arg3);
}

// ------------------------------------------------------------
// Exécution commande
// ------------------------------------------------------------

static bool command_exec_line(const char* line, bool allow_pipe)
{
    static bool rm_pending = false;
    static char rm_target[64];

    if (!line || !*line) {
        return false;
    }

    if (rm_pending) {
        rm_pending = false;
        if (strcmp(line, "y") == 0 || strcmp(line, "Y") == 0) {
            if (fs_rm(rm_target)) {
                term_puts("removed\n");
                return true;
            }
            term_error("cannot remove");
            return false;
        }
        term_puts("cancelled\n");
        return true;
    }

    const char* redir = find_unquoted_char(line, '>');
    if (redir) {
        bool append = (redir[1] == '>');
        std::string left(line, redir - line);
        std::string right(redir + (append ? 2 : 1));
        left = trim_copy(left);
        right = trim_copy(right);

        if (right.empty()) {
            term_error("missing redirect");
            return false;
        }

        term_capture_start();
        bool ok = command_exec_line(left.c_str(), allow_pipe);
        std::string out = term_capture_buffer();
        term_capture_stop();

        bool wrote = append
            ? fs_append_file(right.c_str(),
                reinterpret_cast<const unsigned char*>(out.data()), out.size())
            : fs_write_file(right.c_str(),
                reinterpret_cast<const unsigned char*>(out.data()), out.size());
        if (!wrote) {
            term_error("cannot write");
            return false;
        }
        return ok;
    }

    if (allow_pipe) {
        const char* pipe = strchr(line, '|');
        if (pipe) {
            std::string left(line, pipe - line);
            std::string right(pipe + 1);
            left = trim_copy(left);
            right = trim_copy(right);

            char pcmd[16];
            char parg1[64];
            char parg2[64];
            char parg3[64];
            parse_line(right.c_str(), pcmd, parg1, parg2, parg3);

            if (strcmp(pcmd, "more") == 0 || strcmp(pcmd, "less") == 0) {
                term_capture_start();
                bool ok = command_exec_line(left.c_str(), false);
                std::string out = term_capture_buffer();
                term_capture_stop();

                if (ok) {
                    if (!out.empty()) {
                        term_pager_start(out);
                    }
                } else {
                    if (!out.empty()) {
                        term_puts(out.c_str());
                    }
                }
                return ok;
            }

            if (strcmp(pcmd, "tee") == 0) {
                const char* out_path = nullptr;
                bool append = false;

                if (strcmp(parg1, "-a") == 0) {
                    append = true;
                    out_path = (*parg2) ? parg2 : nullptr;
                } else {
                    out_path = (*parg1) ? parg1 : nullptr;
                }

                if (!out_path) {
                    term_error("missing operand");
                    return false;
                }

                term_capture_start();
                bool ok = command_exec_line(left.c_str(), false);
                std::string out = term_capture_buffer();
                term_capture_stop();

                bool wrote = append
                    ? fs_append_file(out_path,
                        reinterpret_cast<const unsigned char*>(out.data()), out.size())
                    : fs_write_file(out_path,
                        reinterpret_cast<const unsigned char*>(out.data()), out.size());
                if (!wrote) {
                    term_error("cannot write");
                    return false;
                }

                if (!out.empty()) {
                    term_puts(out.c_str());
                }
                return ok;
            }
        }
    }

    char cmd[16];
    char arg1[64];
    char arg2[64];
    char arg3[64];

    parse_line(line, cmd, arg1, arg2, arg3);

    // --------------------------------------------------------
    // pwd
    // --------------------------------------------------------
    if (strcmp(cmd, "pwd") == 0) {
        term_puts(fs_pwd());
        term_putc('\n');
        return true;
    }

    // --------------------------------------------------------
    // cd [path]
    // --------------------------------------------------------
    if (strcmp(cmd, "cd") == 0) {
        const char* path = (*arg1) ? arg1 : "/";

        if (!fs_cd(path)) {
            term_error("cannot change directory");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // ls [path]
    // --------------------------------------------------------
    if (strcmp(cmd, "ls") == 0) {
        const char* opts = nullptr;
        const char* path = nullptr;

        if (*arg1 && arg1[0] == '-') {
            opts = arg1;
            path = (*arg2) ? arg2 : fs_pwd();
        } else {
            path = (*arg1) ? arg1 : fs_pwd();
        }

        if (!fs_list(path, opts)) {
            term_error("cannot access");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // mount
    // --------------------------------------------------------
    if (strcmp(cmd, "mount") == 0) {
        if (fs_sd_mounted()) {
            term_puts("SDCard already mounted\n");
            return true;
        }

        if (fs_mount()) {
            term_puts("SDCard 0 mounted at /media/0\n");
        } else {
            term_error("no sdcard");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // umount
    // --------------------------------------------------------
    if (strcmp(cmd, "umount") == 0) {
        if (!fs_sd_mounted()) {
            term_error("not mounted");
            return false;
        }

        const char* cwd = fs_pwd();
        if (cwd) {
            const char* mount_root = "/media/0";
            size_t mount_len = strlen(mount_root);
            if (strncmp(cwd, mount_root, mount_len) == 0 &&
                (cwd[mount_len] == '\0' || cwd[mount_len] == '/')) {
                fs_cd("/");
            }
        }

        fs_umount();
        term_puts("SDCard unmounted\n");
        return true;
    }

    // --------------------------------------------------------
    // vi [path]
    // --------------------------------------------------------
    if (strcmp(cmd, "vi") == 0) {
        const char* path = (*arg1) ? arg1 : "";
        editor_open(path);
        return true;
    }

    // --------------------------------------------------------
    // view <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "view") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }

        char real[128];
        if (!fs_resolve_real_path(arg1, real, sizeof(real))) {
            term_error("cannot read");
            return false;
        }

        bool is_png = has_ext(real, ".png");
        bool is_jpg = has_ext(real, ".jpg") || has_ext(real, ".jpeg");
        if (!is_png && !is_jpg) {
            term_error("unsupported format");
            return false;
        }

        screen_clear();
        uint8_t base_rotation = M5.Display.getRotation();
        uint8_t rotation = base_rotation;
        bool ok = view_render_image(real);

        if (!ok) {
            screen_clear();
            term_redraw();
            term_error("cannot read");
            return false;
        }

        for (;;) {
            M5.update();
            M5Cardputer.update();
            M5Cardputer.Keyboard.updateKeyList();
            M5Cardputer.Keyboard.updateKeysState();
            auto &st = M5Cardputer.Keyboard.keysState();
            bool saw_r = false;
            for (char c : st.word) {
                if (c == 'r' || c == 'R') {
                    saw_r = true;
                    break;
                }
            }
            if (saw_r) {
                rotation = (uint8_t)((rotation + 1) % 4);
                M5.Display.setRotation(rotation);
                screen_clear();
                view_render_image(real);
                view_wait_for_char_release('r');
                view_wait_for_char_release('R');
                continue;
            }
            if (view_any_key_pressed_no_fn()) {
                break;
            }
            delay(10);
        }

        M5.Display.setRotation(base_rotation);
        screen_clear();
        term_redraw();
        return true;
    }

    // --------------------------------------------------------
    // slideshow [-t seconds] <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "slideshow") == 0) {
        const char* path = arg1;
        int interval = 0;
        if (strcmp(arg1, "-t") == 0) {
            if (!*arg2 || !*arg3) {
                term_error("usage: slideshow [-t seconds] <path>");
                return false;
            }
            interval = atoi(arg2);
            if (interval < 2) interval = 2;
            if (interval > 120) interval = 120;
            path = arg3;
        }
        if (!*path) {
            term_error("missing operand");
            return false;
        }

        const bool suspend_saver = (interval > 0);
        if (suspend_saver) {
            screensaver_set_suspend(true);
        }

        std::vector<FsEntry> entries;
        if (!fs_list_entries(path, entries, false)) {
            if (suspend_saver) {
                screensaver_set_suspend(false);
            }
            term_error("cannot read");
            return false;
        }

        std::vector<std::string> items;
        for (const auto& entry : entries) {
            if (entry.is_dir) {
                continue;
            }
            std::string name = entry.name;
            std::string lower = name;
            for (char& c : lower) {
                c = (char)tolower((unsigned char)c);
            }
            if (lower.size() >= 4 &&
                (lower.rfind(".png") == lower.size() - 4 ||
                 lower.rfind(".jpg") == lower.size() - 4 ||
                 lower.rfind(".jpeg") == lower.size() - 5)) {
                items.push_back(name);
            }
        }

        if (items.empty()) {
            if (suspend_saver) {
                screensaver_set_suspend(false);
            }
            term_error("no images found");
            return false;
        }

        std::sort(items.begin(), items.end());

        auto make_path = [&](const std::string& name) {
            std::string p = path;
            if (!p.empty() && p.back() != '/') {
                p.push_back('/');
            }
            p += name;
            return p;
        };

        size_t index = 0;
        uint8_t base_rotation = M5.Display.getRotation();
        uint8_t rotation = base_rotation;
        uint32_t next_tick = 0;

        for (;;) {
            std::string item_path = make_path(items[index]);
            char real[128];
            if (!fs_resolve_real_path(item_path.c_str(), real, sizeof(real))) {
                term_error("cannot read");
                break;
            }

            screen_clear();
            if (!view_render_image(real)) {
                term_error("cannot read");
                break;
            }
            if (interval > 0) {
                next_tick = millis() + (uint32_t)interval * 1000U;
            }

            bool advance = false;
            for (;;) {
                int input = slideshow_read_input();
                if (input == 99) {
                    M5.Display.setRotation(base_rotation);
                    screen_clear();
                    term_redraw();
                    if (suspend_saver) {
                        screensaver_set_suspend(false);
                    }
                    return true;
                }
                if (input == 2 && interval == 0) {
                    rotation = (uint8_t)((rotation + 1) % 4);
                    M5.Display.setRotation(rotation);
                    screen_clear();
                    view_render_image(real);
                    view_wait_for_char_release('r');
                    view_wait_for_char_release('R');
                    continue;
                }
                if (input == 1) {
                    index = (index + 1) % items.size();
                    rotation = base_rotation;
                    M5.Display.setRotation(rotation);
                    view_wait_for_nav_release();
                    advance = true;
                    break;
                }
                if (input == -1) {
                    index = (index + items.size() - 1) % items.size();
                    rotation = base_rotation;
                    M5.Display.setRotation(rotation);
                    view_wait_for_nav_release();
                    advance = true;
                    break;
                }
                if (interval > 0 && millis() >= next_tick) {
                    index = (index + 1) % items.size();
                    rotation = base_rotation;
                    M5.Display.setRotation(rotation);
                    next_tick = millis() + (uint32_t)interval * 1000U;
                    advance = true;
                    break;
                }
                delay(10);
            }

            if (!advance) {
                break;
            }
        }

        screen_clear();
        term_redraw();
        if (suspend_saver) {
            screensaver_set_suspend(false);
        }
        return true;
    }

    // --------------------------------------------------------
    // play [-v 0-100] <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "play") == 0) {
        const char* path = arg1;
        int volume = -1;
        if (strcmp(arg1, "-v") == 0) {
            if (!*arg2 || !*arg3) {
                term_error("usage: play [-v 0-100] <path>");
                return false;
            }
            volume = atoi(arg2);
            path = arg3;
        }
        if (!*path) {
            term_error("missing operand");
            return false;
        }

        char real[128];
        if (!fs_resolve_real_path(path, real, sizeof(real))) {
            term_error("cannot read");
            return false;
        }

        bool is_wav = has_ext(real, ".wav");
        bool is_mp3 = has_ext(real, ".mp3");
        if (!is_wav && !is_mp3) {
            term_error("unsupported format");
            return false;
        }

        std::vector<uint8_t> data;
        uint8_t prev_vol = M5.Speaker.getVolume();
        if (volume >= 0) {
            if (volume > 100) volume = 100;
            uint8_t v = (uint8_t)((volume * 255) / 100);
            M5.Speaker.setVolume(v);
        }

        auto spk_cfg = M5.Speaker.config();
        spk_cfg.dma_buf_len = 1024;
        spk_cfg.dma_buf_count = 16;
        spk_cfg.task_priority = 3;
        spk_cfg.task_pinned_core = 0;
        M5.Speaker.config(spk_cfg);
        M5.Speaker.begin();
        if (is_mp3) {
            if (!play_mp3_file(real)) {
                if (volume >= 0) {
                    M5.Speaker.setVolume(prev_vol);
                }
                term_error("cannot play");
                return false;
            }
        } else {
            if (!play_wav_file(real)) {
                if (volume >= 0) {
                    M5.Speaker.setVolume(prev_vol);
                }
                term_error("cannot play");
                return false;
            }
        }

        if (volume >= 0) {
            M5.Speaker.setVolume(prev_vol);
        }
        return true;
    }

    // --------------------------------------------------------
    // nano [path]
    // --------------------------------------------------------
    if (strcmp(cmd, "nano") == 0) {
        const char* path = (*arg1) ? arg1 : "";
        editor_open_with_mode(path, true);
        return true;
    }

    // --------------------------------------------------------
    // touch <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "touch") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        if (!fs_touch(arg1)) {
            term_error("cannot touch");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // cat <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "cat") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        std::string content;
        if (!fs_read_file(arg1, content)) {
            term_error("cannot read");
            return false;
        }
        std::string display = to_cp437(content);
        term_puts(display.c_str());
        if (!display.empty() && display.back() != '\n') {
            term_putc('\n');
        }
        return true;
    }

    // --------------------------------------------------------
    // tee (needs pipe)
    // --------------------------------------------------------
    if (strcmp(cmd, "tee") == 0) {
        term_error("tee requires pipe");
        return false;
    }

    // --------------------------------------------------------
    // lx <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "lx") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        if (strcmp(arg1, "--profile") == 0 || strcmp(arg1, "-p") == 0) {
            if (!*arg2 || !*arg3) {
                term_error("missing operand");
                return false;
            }
            if (!lx_set_profile(arg2)) {
                term_error("bad profile");
                return false;
            }
            return lx_run_script(arg3);
        }
        return lx_run_script(arg1);
    }

    // --------------------------------------------------------
    // more <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "more") == 0 || strcmp(cmd, "less") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        std::string content;
        if (!fs_read_file(arg1, content)) {
            term_error("cannot read");
            return false;
        }
        std::string display = to_cp437(content);
        term_pager_start(display);
        return true;
    }

    // --------------------------------------------------------
    // lxprofile [name]
    // --------------------------------------------------------
    if (strcmp(cmd, "lxprofile") == 0) {
        if (!*arg1) {
            term_puts(lx_get_profile_name());
            term_putc('\n');
            return true;
        }
        if (!lx_set_profile(arg1)) {
            term_error("bad profile");
            return false;
        }
        term_puts("profile set to ");
        term_puts(lx_get_profile_name());
        term_putc('\n');
        return true;
    }

    // --------------------------------------------------------
    // uptime
    // --------------------------------------------------------
    if (strcmp(cmd, "uptime") == 0) {
        unsigned long ms = millis();
        unsigned long sec = ms / 1000;
        unsigned long days = sec / 86400;
        unsigned long hours = (sec % 86400) / 3600;
        unsigned long mins = (sec % 3600) / 60;
        unsigned long secs = sec % 60;

        char buf[48];
        snprintf(buf, sizeof(buf), "%lu days, %02lu:%02lu:%02lu\n",
            days, hours, mins, secs);
        term_puts(buf);
        return true;
    }

    // --------------------------------------------------------
    // battery
    // --------------------------------------------------------
    if (strcmp(cmd, "battery") == 0) {
        int level = M5.Power.getBatteryLevel();
        int mv = M5.Power.getBatteryVoltage();
        auto status = M5.Power.isCharging();
        bool charging = status == m5::Power_Class::is_charging_t::is_charging;
        bool unknown = status == m5::Power_Class::is_charging_t::charge_unknown;

        char buf[64];
        if (unknown) {
            term_puts("Charging: unknown\n");
        } else {
            term_puts(charging ? "Charging: yes\n" : "Charging: no\n");
        }

        if (level >= 0) {
            snprintf(buf, sizeof(buf), "Capacity: %d%%\n", level);
            term_puts(buf);
        } else {
            term_puts("Capacity: n/a\n");
        }

        if (level >= 0 && !unknown) {
            int minutes = charging ? estimate_minutes_to_full(level)
                                   : estimate_minutes_remaining(level);
            int hours = minutes / 60;
            int mins = minutes % 60;
            snprintf(buf, sizeof(buf), "Time left: %d:%02d\n", hours, mins);
            term_puts(buf);
        } else {
            term_puts("Time left: n/a\n");
        }

        if (mv > 0) {
            snprintf(buf, sizeof(buf), "Voltage: %d mV\n", mv);
            term_puts(buf);
        } else {
            term_puts("Voltage: n/a\n");
        }
        return true;
    }

    // --------------------------------------------------------
    // clear / reset
    // --------------------------------------------------------
    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "reset") == 0) {
        term_init();
        term_prompt();
        return true;
    }

    // --------------------------------------------------------
    // shutdown [-h|-r]
    // --------------------------------------------------------
    if (strcmp(cmd, "shutdown") == 0) {
        const char* opt = (*arg1) ? arg1 : "-h";
        if (strcmp(opt, "-r") == 0) {
            term_puts("Restarting...\n");
            ESP.restart();
            return true;
        }
        if (strcmp(opt, "-h") == 0) {
            term_puts("Halting...\n");
            M5.Power.powerOff();
            return true;
        }
        term_error("bad option");
        return false;
    }

    // --------------------------------------------------------
    // man <cmd>
    // --------------------------------------------------------
    if (strcmp(cmd, "man") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        std::string text = man_entry(arg1);
        if (text.empty()) {
            term_error("no manual entry");
            return false;
        }
        term_pager_start(text);
        return true;
    }

    // --------------------------------------------------------
    // find <path> [-name|-iname pattern]
    // --------------------------------------------------------
    if (strcmp(cmd, "find") == 0) {
        const char* path = (*arg1) ? arg1 : ".";
        const char* opt = (*arg2) ? arg2 : "";

        if ((strcmp(path, ".") == 0 || strcmp(path, "./") == 0) && strcmp(fs_pwd(), "/") == 0) {
            if (fs_sd_mounted()) {
                path = "/media/0";
            }
        }

        if (!*opt) {
            if (!fs_find(path, nullptr, false)) {
                term_error("cannot access");
                return false;
            }
            return true;
        }

        if (strcmp(opt, "-name") == 0 || strcmp(opt, "-iname") == 0) {
            const char* pattern = (*arg3) ? arg3 : "";
            if (!*pattern) {
                term_error("missing pattern");
                return false;
            }

            bool ci = (strcmp(opt, "-iname") == 0);
            if (!fs_find(path, pattern, ci)) {
                term_error("cannot access");
                return false;
            }
            return true;
        }

        term_error("bad option");
        return false;
    }

    // --------------------------------------------------------
    // mkdir <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "mkdir") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        if (!fs_mkdir(arg1)) {
            term_error("cannot create");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // rmdir <path>
    // --------------------------------------------------------
    if (strcmp(cmd, "rmdir") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        if (!fs_rmdir(arg1)) {
            term_error("cannot remove");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // cp <src> <dst>
    // --------------------------------------------------------
    if (strcmp(cmd, "cp") == 0) {
        if (!*arg1 || !*arg2) {
            term_error("missing operand");
            return false;
        }
        if (!fs_cp(arg1, arg2)) {
            term_error("cannot copy");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // mv <src> <dst>
    // --------------------------------------------------------
    if (strcmp(cmd, "mv") == 0) {
        if (!*arg1 || !*arg2) {
            term_error("missing operand");
            return false;
        }
        if (!fs_mv(arg1, arg2)) {
            term_error("cannot move");
            return false;
        }
        return true;
    }

    // --------------------------------------------------------
    // rm <path> (confirmation)
    // --------------------------------------------------------
    if (strcmp(cmd, "rm") == 0) {
        if (!*arg1) {
            term_error("missing operand");
            return false;
        }
        rm_pending = true;
        strncpy(rm_target, arg1, sizeof(rm_target));
        rm_target[sizeof(rm_target) - 1] = 0;
        term_puts("rm: remove '");
        term_puts(arg1);
        term_puts("'? (y/n)\n");
        return true;
    }

    // --------------------------------------------------------
    // commande inconnue
    // --------------------------------------------------------
    term_error("command not found");
    return false;
}

bool command_exec(const char* line)
{
    return command_exec_line(line, true);
}
