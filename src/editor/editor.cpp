#include "editor.h"

#include "ui/screen.h"
#include "ui/terminal.h"
#include "ui/encoding.h"
#include "fs/fs.h"

#include <M5Unified.h>

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

static constexpr int EDIT_COLS = 30;
static constexpr int EDIT_ROWS = 8;
static constexpr int TEXT_ROWS = EDIT_ROWS - 1;
static constexpr int TAB_WIDTH = 4;

enum EditorMode {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_SEARCH,
    MODE_PROMPT
};

static bool active = false;
static EditorMode mode = MODE_NORMAL;
static bool nano_mode = false;

enum PromptKind {
    PROMPT_NONE,
    PROMPT_WRITE
};

static PromptKind prompt_kind = PROMPT_NONE;
static std::string prompt_label;
static std::string prompt_buffer;
static bool nano_pending_quit = false;

static std::vector<std::string> lines;
static int cur_row = 0;
static int cur_col = 0;
static int view_top_line = 0;
static int view_top_sub = 0;

static std::string current_file;
static bool dirty = false;

static std::string cmd_buffer;
static std::string search_buffer;
static std::string last_search;
static std::string yank_line;
static std::string normal_count;

static bool pending_delete = false;
static bool pending_replace = false;
static char pending_shift = 0;

static std::string status_msg;

static uint16_t fg_color = TFT_DARKGRAY;
static uint16_t bg_color = TFT_BLACK;
static uint16_t cursor_color = TFT_NAVY;
static uint16_t status_fg = TFT_BLACK;
static uint16_t status_bg = TFT_DARKGRAY;

static void set_status(const char* msg)
{
    status_msg = msg ? msg : "";
}

static std::string make_abs_path(const std::string& path)
{
    if (path.empty()) {
        return "";
    }

    if (!path.empty() && path[0] == '/') {
        return path;
    }

    const char* cwd = fs_pwd();
    if (strcmp(cwd, "/") == 0) {
        return "/" + path;
    }

    return std::string(cwd) + "/" + path;
}

static bool map_to_real_path(const std::string& abs_path, std::string& out_real)
{
    if (abs_path.rfind("/media/0", 0) == 0) {
        if (abs_path.size() == strlen("/media/0")) {
            out_real = "/sdcard";
            return true;
        }
        if (abs_path.size() > strlen("/media/0/")) {
            out_real = "/sdcard/" + abs_path.substr(strlen("/media/0/"));
            return true;
        }
    }

    return false;
}

static void ensure_line_exists()
{
    if (lines.empty()) {
        lines.push_back("");
    }
}

static int line_visual_len(const std::string& line)
{
    int len = 0;
    for (char ch : line) {
        len += (ch == '\t') ? TAB_WIDTH : 1;
    }
    return len;
}

static int line_visual_rows(const std::string& line)
{
    int len = line_visual_len(line);
    return (len <= 0) ? 1 : (1 + (len - 1) / EDIT_COLS);
}

static int line_visual_col_for_col(const std::string& line, int col)
{
    int vcol = 0;
    int limit = col;
    if (limit < 0) limit = 0;
    if (limit > (int)line.size()) limit = (int)line.size();
    for (int i = 0; i < limit; i++) {
        vcol += (line[i] == '\t') ? TAB_WIDTH : 1;
    }
    return vcol;
}

static int prefix_visual_rows(int line_idx)
{
    int rows = 0;
    int max_idx = (int)lines.size();
    if (line_idx < 0) line_idx = 0;
    if (line_idx > max_idx) line_idx = max_idx;
    for (int i = 0; i < line_idx; i++) {
        rows += line_visual_rows(lines[i]);
    }
    return rows;
}

static void set_view_abs(int abs_row)
{
    if (abs_row < 0) abs_row = 0;
    int line_idx = 0;
    while (line_idx < (int)lines.size()) {
        int rows = line_visual_rows(lines[line_idx]);
        if (abs_row < rows) {
            view_top_line = line_idx;
            view_top_sub = abs_row;
            return;
        }
        abs_row -= rows;
        line_idx++;
    }
    if (lines.empty()) {
        view_top_line = 0;
        view_top_sub = 0;
    } else {
        view_top_line = (int)lines.size() - 1;
        view_top_sub = 0;
    }
}

static void clamp_cursor()
{
    ensure_line_exists();

    if (cur_row < 0) cur_row = 0;
    if (cur_row >= (int)lines.size()) cur_row = (int)lines.size() - 1;

    int len = (int)lines[cur_row].size();
    bool allow_past_end = (mode == MODE_INSERT) || nano_mode;
    if (allow_past_end) {
        if (cur_col < 0) cur_col = 0;
        if (cur_col > len) cur_col = len;
    } else {
        if (len == 0) {
            cur_col = 0;
        } else {
            if (cur_col < 0) cur_col = 0;
            if (cur_col >= len) cur_col = len - 1;
        }
    }
}

static void ensure_cursor_visible()
{
    ensure_line_exists();
    int cursor_vcol = line_visual_col_for_col(lines[cur_row], cur_col);
    int cursor_vrow = cursor_vcol / EDIT_COLS;
    int cursor_abs = prefix_visual_rows(cur_row) + cursor_vrow;
    int view_abs = prefix_visual_rows(view_top_line) + view_top_sub;

    if (cursor_abs < view_abs) {
        set_view_abs(cursor_abs);
    } else if (cursor_abs >= view_abs + TEXT_ROWS) {
        set_view_abs(cursor_abs - TEXT_ROWS + 1);
    }

    if (view_top_line < 0) view_top_line = 0;
    if (view_top_sub < 0) view_top_sub = 0;
}

static void draw_cell(int col, int row, char ch, bool cursor, bool status_line)
{
    if (status_line) {
        screen_set_color(status_fg, status_bg);
    } else if (cursor) {
        screen_set_color(bg_color, cursor_color);
    } else {
        screen_set_color(fg_color, bg_color);
    }

    char s[2] = { ch, 0 };
    screen_draw_text(col, row, s);
}

static void render_line_segment(const std::string& line, int wrap_row, char* out)
{
    for (int i = 0; i < EDIT_COLS; i++) {
        out[i] = ' ';
    }

    int start = wrap_row * EDIT_COLS;
    int end = start + EDIT_COLS;
    int vcol = 0;
    for (char ch : line) {
        int span = (ch == '\t') ? TAB_WIDTH : 1;
        for (int i = 0; i < span; i++) {
            if (vcol >= start && vcol < end) {
                out[vcol - start] = (ch == '\t') ? ' ' : ch;
            }
            vcol++;
        }
        if (vcol >= end) {
            break;
        }
    }
}

static void redraw()
{
    screen_clear();

    int cursor_vcol = 0;
    int cursor_vrow = 0;
    if (cur_row >= 0 && cur_row < (int)lines.size()) {
        cursor_vcol = line_visual_col_for_col(lines[cur_row], cur_col);
        cursor_vrow = cursor_vcol / EDIT_COLS;
    }

    int line_idx = view_top_line;
    int wrap_row = view_top_sub;
    for (int r = 0; r < TEXT_ROWS; r++) {
        char row_buf[EDIT_COLS];
        bool has_line = (line_idx >= 0 && line_idx < (int)lines.size());
        if (has_line) {
            render_line_segment(lines[line_idx], wrap_row, row_buf);
        } else {
            for (int i = 0; i < EDIT_COLS; i++) {
                row_buf[i] = ' ';
            }
        }

        for (int c = 0; c < EDIT_COLS; c++) {
            bool cursor = false;
            if (has_line && line_idx == cur_row && wrap_row == cursor_vrow &&
                mode != MODE_COMMAND && mode != MODE_SEARCH) {
                cursor = (c == (cursor_vcol % EDIT_COLS));
            }
            draw_cell(c, r, row_buf[c], cursor, false);
        }

        if (has_line) {
            int rows = line_visual_rows(lines[line_idx]);
            wrap_row++;
            if (wrap_row >= rows) {
                line_idx++;
                wrap_row = 0;
            }
        } else {
            line_idx++;
        }
    }

    std::string status;
    if (mode == MODE_COMMAND) {
        status = ":" + cmd_buffer;
    } else if (mode == MODE_SEARCH) {
        status = nano_mode ? "Search: " + search_buffer : "/" + search_buffer;
    } else if (mode == MODE_PROMPT) {
        status = prompt_label + prompt_buffer;
    } else if (!status_msg.empty()) {
        status = status_msg;
    } else if (nano_mode) {
        status = "^O Wr ^X Ex ^W Fi ^K Cu ^U Pu";
    } else {
        status = (mode == MODE_INSERT) ? "INSERT" : "NORMAL";
        if (!current_file.empty()) {
            std::string path = current_file;
            int room = EDIT_COLS - (int)status.size() - 1;
            if (room > 0 && (int)path.size() > room) {
                int tail_len = room - 3;
                if (tail_len < 0) {
                    tail_len = 0;
                }
                if (tail_len > 0 && tail_len < (int)path.size()) {
                    path = "..." + path.substr(path.size() - (size_t)tail_len);
                } else {
                    path = "...";
                }
            }
            status += " ";
            status += path;
        }
        if (dirty) {
            status += " [+]";
        }
    }

    if ((int)status.size() > EDIT_COLS) {
        status = status.substr(0, EDIT_COLS);
    }

    for (int c = 0; c < EDIT_COLS; c++) {
        char ch = (c < (int)status.size()) ? status[c] : ' ';
        draw_cell(c, EDIT_ROWS - 1, ch, false, true);
    }
}

void editor_redraw()
{
    if (!active) {
        return;
    }
    redraw();
}

static bool read_file(const std::string& abs_path)
{
    std::string real_path;
    if (!map_to_real_path(abs_path, real_path)) {
        set_status("unsupported path");
        return false;
    }

    FILE* f = fopen(real_path.c_str(), "rb");
    lines.clear();

    if (!f) {
        ensure_line_exists();
        set_status("new file");
        return true;
    }

    std::string raw_line;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') {
            std::vector<char> conv(raw_line.size() + 1);
            size_t len = utf8_to_cp437(raw_line.c_str(), conv.data(), conv.size());
            lines.emplace_back(conv.data(), len);
            raw_line.clear();
        } else if (ch != '\r') {
            raw_line.push_back((char)ch);
        }
    }
    std::vector<char> conv(raw_line.size() + 1);
    size_t len = utf8_to_cp437(raw_line.c_str(), conv.data(), conv.size());
    lines.emplace_back(conv.data(), len);
    fclose(f);

    ensure_line_exists();
    return true;
}

static bool write_file(const std::string& abs_path)
{
    std::string real_path;
    if (!map_to_real_path(abs_path, real_path)) {
        set_status("unsupported path");
        return false;
    }

    FILE* f = fopen(real_path.c_str(), "wb");
    if (!f) {
        set_status("write failed");
        return false;
    }

    for (size_t i = 0; i < lines.size(); i++) {
        if (!lines[i].empty()) {
            fwrite(lines[i].data(), 1, lines[i].size(), f);
        }
        fputc('\n', f);
    }
    fclose(f);

    dirty = false;
    set_status("written");
    return true;
}

static bool find_next(const std::string& needle)
{
    if (needle.empty()) {
        return false;
    }

    int r = cur_row;
    int c = cur_col + 1;

    for (int pass = 0; pass < 2; pass++) {
        for (; r < (int)lines.size(); r++) {
            const std::string& line = lines[r];
            size_t start = (r == cur_row) ? (size_t)c : 0;
            size_t pos = line.find(needle, start);
            if (pos != std::string::npos) {
                cur_row = r;
                cur_col = (int)pos;
                clamp_cursor();
                ensure_cursor_visible();
                return true;
            }
        }
        r = 0;
        c = 0;
    }

    return false;
}

static bool find_prev(const std::string& needle)
{
    if (needle.empty()) {
        return false;
    }

    int r = cur_row;
    int c = cur_col - 1;

    for (int pass = 0; pass < 2; pass++) {
        for (; r >= 0; r--) {
            const std::string& line = lines[r];
            int end = (r == cur_row) ? c : (int)line.size() - 1;
            if (end < 0) continue;
            size_t pos = line.rfind(needle, (size_t)end);
            if (pos != std::string::npos) {
                cur_row = r;
                cur_col = (int)pos;
                clamp_cursor();
                ensure_cursor_visible();
                return true;
            }
        }
        r = (int)lines.size() - 1;
        c = (r >= 0) ? (int)lines[r].size() - 1 : 0;
    }

    return false;
}

static void delete_current_line()
{
    if (lines.empty()) return;

    yank_line = lines[cur_row];
    lines.erase(lines.begin() + cur_row);
    if (lines.empty()) {
        lines.push_back("");
        cur_row = 0;
    } else if (cur_row >= (int)lines.size()) {
        cur_row = (int)lines.size() - 1;
    }
    cur_col = 0;
    dirty = true;
}

static void paste_line_below()
{
    if (yank_line.empty() && lines.size() > 0) {
        return;
    }
    if (lines.empty()) {
        lines.push_back(yank_line);
        cur_row = 0;
    } else {
        lines.insert(lines.begin() + cur_row + 1, yank_line);
        cur_row++;
    }
    cur_col = 0;
    dirty = true;
}

static void substitute_current_line(const std::string& from, const std::string& to, bool global)
{
    if (lines.empty()) return;

    std::string& line = lines[cur_row];
    if (from.empty()) {
        return;
    }

    size_t pos = line.find(from);
    if (pos == std::string::npos) {
        set_status("pattern not found");
        return;
    }

    if (global) {
        while (pos != std::string::npos) {
            line.replace(pos, from.size(), to);
            pos = line.find(from, pos + to.size());
        }
    } else {
        line.replace(pos, from.size(), to);
    }
    dirty = true;
}

static void handle_command()
{
    std::string cmd = cmd_buffer;
    cmd_buffer.clear();
    mode = MODE_NORMAL;

    while (!cmd.empty() && cmd[0] == ' ') {
        cmd.erase(0, 1);
    }

    if (cmd == "q") {
        if (dirty) {
            set_status("no write since last change");
        } else {
            editor_close();
        }
        return;
    }

    if (cmd == "q!") {
        editor_close();
        return;
    }

    bool all_digits = !cmd.empty();
    for (char ch : cmd) {
        if (ch < '0' || ch > '9') {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        int line_num = atoi(cmd.c_str());
        if (line_num < 1) {
            set_status("bad line");
            return;
        }
        if (line_num > (int)lines.size()) {
            line_num = (int)lines.size();
        }
        cur_row = line_num - 1;
        cur_col = 0;
        clamp_cursor();
        ensure_cursor_visible();
        return;
    }

    if (cmd == "wq" || cmd == "x") {
        if (current_file.empty()) {
            set_status("no file name");
            return;
        }
        if (write_file(make_abs_path(current_file))) {
            editor_close();
        }
        return;
    }

    if (cmd.rfind("w", 0) == 0) {
        std::string path = cmd.size() > 1 ? cmd.substr(1) : "";
        while (!path.empty() && path[0] == ' ') {
            path.erase(0, 1);
        }
        if (path.empty()) {
            if (current_file.empty()) {
                set_status("no file name");
                return;
            }
            write_file(make_abs_path(current_file));
        } else {
            current_file = make_abs_path(path);
            write_file(current_file);
        }
        return;
    }

    if (cmd.rfind("e", 0) == 0) {
        std::string path = cmd.size() > 1 ? cmd.substr(1) : "";
        while (!path.empty() && path[0] == ' ') {
            path.erase(0, 1);
        }
        if (path.empty()) {
            set_status("no file name");
            return;
        }
        editor_open(path.c_str());
        return;
    }

    if (cmd.rfind("s/", 0) == 0) {
        std::string rest = cmd.substr(2);
        size_t p1 = rest.find('/');
        if (p1 == std::string::npos) {
            set_status("bad substitute");
            return;
        }
        std::string from = rest.substr(0, p1);
        std::string rest2 = rest.substr(p1 + 1);
        size_t p2 = rest2.find('/');
        if (p2 == std::string::npos) {
            set_status("bad substitute");
            return;
        }
        std::string to = rest2.substr(0, p2);
        std::string flags = rest2.substr(p2 + 1);
        bool global = (flags.find('g') != std::string::npos);
        substitute_current_line(from, to, global);
        return;
    }

    set_status("unknown command");
}

void editor_init()
{
    active = false;
    mode = MODE_NORMAL;
    nano_mode = false;
    prompt_kind = PROMPT_NONE;
    prompt_label.clear();
    prompt_buffer.clear();
    nano_pending_quit = false;
    lines.clear();
    current_file.clear();
    dirty = false;
    cmd_buffer.clear();
    search_buffer.clear();
    last_search.clear();
    yank_line.clear();
    normal_count.clear();
    pending_delete = false;
    pending_replace = false;
    status_msg.clear();
}

bool editor_is_active()
{
    return active;
}

static void editor_open_impl(const char* path, bool use_nano)
{
    active = true;
    nano_mode = use_nano;
    mode = use_nano ? MODE_INSERT : MODE_NORMAL;
    prompt_kind = PROMPT_NONE;
    prompt_label.clear();
    prompt_buffer.clear();
    nano_pending_quit = false;
    pending_delete = false;
    pending_replace = false;
    pending_shift = 0;
    normal_count.clear();
    status_msg.clear();

    current_file = make_abs_path(path ? path : "");

    if (!current_file.empty()) {
        read_file(current_file);
    } else {
        lines.clear();
        ensure_line_exists();
    }

    cur_row = 0;
    cur_col = 0;
    view_top_line = 0;
    view_top_sub = 0;
    dirty = false;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_open(const char* path)
{
    editor_open_impl(path, false);
}

void editor_open_with_mode(const char* path, bool use_nano)
{
    editor_open_impl(path, use_nano);
}

static void shift_current_line(int dir)
{
    ensure_line_exists();
    std::string& line = lines[cur_row];
    const int shift = 4;

    if (dir > 0) {
        line.insert(0, (size_t)shift, ' ');
        cur_col += shift;
        dirty = true;
        return;
    }

    if (line.empty()) {
        return;
    }

    if (line[0] == '\t') {
        line.erase(0, 1);
        if (cur_col > 0) cur_col--;
        dirty = true;
        return;
    }

    int remove = 0;
    while (remove < shift && remove < (int)line.size() && line[remove] == ' ') {
        remove++;
    }
    if (remove > 0) {
        line.erase(0, (size_t)remove);
        if (cur_col >= remove) cur_col -= remove;
        else cur_col = 0;
        dirty = true;
    }
}

static void join_line_below()
{
    if (lines.empty()) {
        return;
    }
    if (cur_row < 0 || cur_row >= (int)lines.size() - 1) {
        return;
    }
    std::string& line = lines[cur_row];
    std::string& next = lines[cur_row + 1];
    if (!line.empty() && !next.empty() && line.back() != ' ') {
        line.push_back(' ');
    }
    line += next;
    lines.erase(lines.begin() + cur_row + 1);
    dirty = true;
}

static bool is_word_char(char ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) return true;
    if (ch >= '0' && ch <= '9') return true;
    return ch == '_' || ch == '-';
}

static void move_to_next_word()
{
    ensure_line_exists();
    std::string& line = lines[cur_row];
    int row = cur_row;
    int col = cur_col;

    auto advance_line = [&]() {
        if (row + 1 < (int)lines.size()) {
            row++;
            col = 0;
            line = lines[row];
            return true;
        }
        return false;
    };

    int len = (int)line.size();
    if (col < len && is_word_char(line[(size_t)col])) {
        while (col < len && is_word_char(line[(size_t)col])) {
            col++;
        }
    }
    while (true) {
        len = (int)line.size();
        while (col < len && !is_word_char(line[(size_t)col])) {
            col++;
        }
        if (col < len) {
            break;
        }
        if (!advance_line()) {
            break;
        }
    }

    cur_row = row;
    cur_col = col;
}

void editor_close()
{
    active = false;
    term_init();
    term_prompt();
}

void editor_handle_char(char c)
{
    if (!active) return;

    if (!status_msg.empty() && (mode == MODE_NORMAL || nano_mode)) {
        status_msg.clear();
    }

    if (nano_mode) {
        nano_pending_quit = false;

        if (mode == MODE_PROMPT) {
            if (c >= 32 && c <= 126) {
                prompt_buffer.push_back(c);
            }
            redraw();
            return;
        }

        if (mode == MODE_SEARCH) {
            if (c >= 32 && c <= 126) {
                search_buffer.push_back(c);
            }
            redraw();
            return;
        }

        if (mode != MODE_INSERT) {
            return;
        }

        if (c >= 32 && c <= 126) {
            ensure_line_exists();
            lines[cur_row].insert((size_t)cur_col, 1, c);
            cur_col++;
            dirty = true;
        }
        clamp_cursor();
        ensure_cursor_visible();
        redraw();
        return;
    }

    if (mode == MODE_COMMAND) {
        if (c >= 32 && c <= 126) {
            cmd_buffer.push_back(c);
        }
        redraw();
        return;
    }

    if (mode == MODE_SEARCH) {
        if (c >= 32 && c <= 126) {
            search_buffer.push_back(c);
        }
        redraw();
        return;
    }

    if (mode == MODE_INSERT) {
        if (c >= 32 && c <= 126) {
            ensure_line_exists();
            lines[cur_row].insert((size_t)cur_col, 1, c);
            cur_col++;
            dirty = true;
        }
        clamp_cursor();
        ensure_cursor_visible();
        redraw();
        return;
    }

    if (c >= '0' && c <= '9') {
        normal_count.push_back(c);
        return;
    }

    if (pending_replace) {
        pending_replace = false;
        if (c >= 32 && c <= 126 && !lines.empty()) {
            std::string& line = lines[cur_row];
            if (!line.empty() && cur_col >= 0 && cur_col < (int)line.size()) {
                line[(size_t)cur_col] = c;
                dirty = true;
            }
        }
        redraw();
        return;
    }

    if (pending_delete) {
        pending_delete = false;
        if (c == 'd') {
            delete_current_line();
            clamp_cursor();
            ensure_cursor_visible();
            redraw();
        }
        return;
    }

    if (pending_shift) {
        char want = pending_shift;
        pending_shift = 0;
        if (c == want) {
            shift_current_line(want == '>' ? 1 : -1);
            clamp_cursor();
            ensure_cursor_visible();
            redraw();
            return;
        }
    }

    switch (c) {
        case 'i':
            mode = MODE_INSERT;
            break;
        case 'a':
            if (!lines.empty()) {
                int len = (int)lines[cur_row].size();
                if (cur_col < len) cur_col++;
            }
            mode = MODE_INSERT;
            break;
        case 'o':
            if (lines.empty()) {
                lines.push_back("");
                cur_row = 0;
            } else {
                lines.insert(lines.begin() + cur_row + 1, "");
                cur_row++;
            }
            cur_col = 0;
            dirty = true;
            mode = MODE_INSERT;
            break;
        case 'x': {
            if (!lines.empty()) {
                std::string& line = lines[cur_row];
                if (!line.empty() && cur_col >= 0 && cur_col < (int)line.size()) {
                    line.erase((size_t)cur_col, 1);
                    dirty = true;
                }
            }
            break;
        }
        case 'r':
            pending_replace = true;
            break;
        case 'p':
            paste_line_below();
            break;
        case 'd':
            pending_delete = true;
            break;
        case '>':
        case '<':
            pending_shift = c;
            return;
        case ':':
            mode = MODE_COMMAND;
            cmd_buffer.clear();
            break;
        case '/':
            mode = MODE_SEARCH;
            search_buffer.clear();
            break;
        case 'n':
            if (last_search.empty()) {
                set_status("no previous search");
            } else if (!find_next(last_search)) {
                set_status("pattern not found");
            }
            break;
        case 'N':
            if (last_search.empty()) {
                set_status("no previous search");
            } else if (!find_prev(last_search)) {
                set_status("pattern not found");
            }
            break;
        case 'G': {
            int target = -1;
            if (!normal_count.empty()) {
                target = atoi(normal_count.c_str());
                if (target < 1) target = 1;
                if (target > (int)lines.size()) target = (int)lines.size();
                cur_row = target - 1;
            } else {
                cur_row = (int)lines.size() - 1;
            }
            cur_col = 0;
            break;
        }
        case 'J':
            join_line_below();
            break;
        case '^':
            cur_col = 0;
            break;
        case '$':
            if (!lines.empty()) {
                int len = (int)lines[cur_row].size();
                cur_col = (len > 0) ? len - 1 : 0;
            } else {
                cur_col = 0;
            }
            break;
        case 'w':
            move_to_next_word();
            break;
        case 'h':
            editor_cursor_left();
            return;
        case 'j':
            editor_cursor_down();
            return;
        case 'k':
            editor_cursor_up();
            return;
        case 'l':
            editor_cursor_right();
            return;
        default:
            break;
    }

    normal_count.clear();
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_handle_char_raw(uint8_t c)
{
    if (!active) return;

    if (!status_msg.empty() && (mode == MODE_NORMAL || nano_mode)) {
        status_msg.clear();
    }

    if (nano_mode) {
        nano_pending_quit = false;
    }

    if (mode != MODE_INSERT) {
        return;
    }

    ensure_line_exists();
    lines[cur_row].insert((size_t)cur_col, 1, (char)c);
    cur_col++;
    dirty = true;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_handle_backspace()
{
    if (!active) return;

    if (nano_mode) {
        nano_pending_quit = false;
    }

    if (mode == MODE_PROMPT) {
        if (!prompt_buffer.empty()) prompt_buffer.pop_back();
        redraw();
        return;
    }

    if (mode == MODE_COMMAND) {
        if (!cmd_buffer.empty()) cmd_buffer.pop_back();
        redraw();
        return;
    }

    if (mode == MODE_SEARCH) {
        if (!search_buffer.empty()) search_buffer.pop_back();
        redraw();
        return;
    }

    if (mode != MODE_INSERT) {
        return;
    }

    if (cur_col > 0) {
        lines[cur_row].erase((size_t)cur_col - 1, 1);
        cur_col--;
        dirty = true;
    } else if (cur_row > 0) {
        int prev_len = (int)lines[cur_row - 1].size();
        lines[cur_row - 1] += lines[cur_row];
        lines.erase(lines.begin() + cur_row);
        cur_row--;
        cur_col = prev_len;
        dirty = true;
    }

    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_handle_enter()
{
    if (!active) return;

    if (nano_mode) {
        nano_pending_quit = false;
    }

    if (mode == MODE_PROMPT) {
        if (prompt_kind == PROMPT_WRITE) {
            std::string path = prompt_buffer;
            while (!path.empty() && path[0] == ' ') {
                path.erase(0, 1);
            }
            if (path.empty()) {
                if (current_file.empty()) {
                    set_status("no file name");
                } else {
                    write_file(make_abs_path(current_file));
                }
            } else {
                current_file = make_abs_path(path);
                write_file(current_file);
            }
        }
        prompt_kind = PROMPT_NONE;
        prompt_label.clear();
        prompt_buffer.clear();
        mode = nano_mode ? MODE_INSERT : MODE_NORMAL;
        redraw();
        return;
    }

    if (mode == MODE_COMMAND) {
        handle_command();
        if (editor_is_active()) {
            redraw();
        }
        return;
    }

    if (mode == MODE_SEARCH) {
        last_search = search_buffer;
        search_buffer.clear();
        mode = nano_mode ? MODE_INSERT : MODE_NORMAL;
        if (!find_next(last_search)) {
            set_status("pattern not found");
        }
        redraw();
        return;
    }

    if (mode != MODE_INSERT) {
        return;
    }

    ensure_line_exists();
    std::string& line = lines[cur_row];
    std::string tail = line.substr((size_t)cur_col);
    line.erase((size_t)cur_col);
    lines.insert(lines.begin() + cur_row + 1, tail);
    cur_row++;
    cur_col = 0;
    dirty = true;

    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_handle_escape()
{
    if (!active) return;

    pending_delete = false;
    pending_replace = false;
    normal_count.clear();
    nano_pending_quit = false;

    if (nano_mode) {
        mode = MODE_INSERT;
        prompt_kind = PROMPT_NONE;
        prompt_label.clear();
        prompt_buffer.clear();
        cmd_buffer.clear();
        search_buffer.clear();
        redraw();
        return;
    }

    if (mode != MODE_NORMAL) {
        mode = MODE_NORMAL;
        cmd_buffer.clear();
        search_buffer.clear();
    }

    redraw();
}

void editor_handle_delete()
{
    if (!active) return;

    if (mode == MODE_INSERT) {
        if (!lines.empty()) {
            std::string& line = lines[cur_row];
            if (cur_col >= 0 && cur_col < (int)line.size()) {
                line.erase((size_t)cur_col, 1);
                dirty = true;
            }
        }
        redraw();
    }
}

void editor_cursor_up()
{
    if (!active) return;
    cur_row--;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_cursor_down()
{
    if (!active) return;
    cur_row++;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_cursor_left()
{
    if (!active) return;
    cur_col--;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_cursor_right()
{
    if (!active) return;
    cur_col++;
    clamp_cursor();
    ensure_cursor_visible();
    redraw();
}

void editor_handle_ctrl(uint8_t c)
{
    if (!active || !nano_mode) return;

    if (c >= 'A' && c <= 'Z') {
        c = (uint8_t)(c - 'A' + 'a');
    }

    switch (c) {
        case 'x':
            if (dirty && !nano_pending_quit) {
                nano_pending_quit = true;
                set_status("modified: Ctrl+O write, Ctrl+X exit");
                redraw();
                return;
            }
            editor_close();
            return;
        case 'o':
            nano_pending_quit = false;
            mode = MODE_PROMPT;
            prompt_kind = PROMPT_WRITE;
            prompt_label = "Write: ";
            prompt_buffer = current_file;
            redraw();
            return;
        case 'w':
            nano_pending_quit = false;
            mode = MODE_SEARCH;
            search_buffer.clear();
            redraw();
            return;
        case 'k':
            nano_pending_quit = false;
            delete_current_line();
            clamp_cursor();
            ensure_cursor_visible();
            set_status("cut");
            redraw();
            return;
        case 'u':
            nano_pending_quit = false;
            paste_line_below();
            clamp_cursor();
            ensure_cursor_visible();
            set_status("pasted");
            redraw();
            return;
        case 'g':
            nano_pending_quit = false;
            set_status("nano: ^O write ^X exit ^W find ^K cut ^U paste");
            redraw();
            return;
        default:
            return;
    }
}
