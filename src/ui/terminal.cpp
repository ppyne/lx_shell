#include "terminal.h"
#include "screen.h"
#include "core/command.h"
#include "editor/editor.h"
#include "ui/encoding.h"


#include <M5Unified.h>
#include <Arduino.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdio.h>

#include "fs/fs.h"

#define TERM_COLS 30
#define TERM_ROWS 8

static char buffer[TERM_ROWS][TERM_COLS + 1];

static bool cursor_visible = true;
static int cur_row = 0;
static int cur_col = 0;
static int prev_cur_row = 0;
static int prev_cur_col = 0;

static constexpr int TFT_DARKRED = 0x9800; /* 160,   0,   0 */

static uint16_t line_color[TERM_ROWS];
static uint16_t default_fg = TFT_DARKGRAY;
static uint16_t default_bg = TFT_BLACK;
static uint16_t default_cursor = TFT_NAVY;
static uint16_t default_prompt = TFT_DARKGREEN;
static uint16_t default_error = TFT_DARKRED;
static uint16_t current_fg = TFT_DARKGRAY;

static std::string current_line;
static std::vector<std::string> history;
static int history_index = -1;
static std::string history_saved_line;

static const char* history_real_path = "/sdcard/.lx_history";
static const size_t kHistoryMaxLines = 1000;

static bool capture_active = false;
static std::string capture_buffer;

static bool raw_input_active = false;
static int raw_start_row = 0;
static int raw_start_col = 0;

static bool pager_active = false;
static std::vector<std::string> pager_lines;
static int pager_index = 0;
static int input_row = 0;
static int input_rows = 1;

// ------------------------------------------------------------
// Affichage
// ------------------------------------------------------------

static void redraw_row(int r)
{
    char line_cp437[TERM_COLS + 1];
    int start_col = 0;

    // Conversion UTF-8 -> CP437 de la ligne complète
    utf8_to_cp437(buffer[r], line_cp437, sizeof(line_cp437));

    // ----- PROMPT -----
    if (!pager_active && line_cp437[0] == '$') {
        screen_set_color(default_prompt, default_bg);
        char p[3] = { '$', ' ', 0 };
        screen_draw_text(0, r, p);
        start_col = 2;
    }

    // ----- TEXTE + CURSEUR -----
    for (int c = start_col; c < TERM_COLS; c++) {
        char ch = line_cp437[c];
        if (ch == 0) ch = ' ';

        if (!pager_active && r == cur_row && c == cur_col) {
            screen_set_color(default_bg, default_cursor);
        } else {
            screen_set_color(line_color[r], default_bg);
        }

        char s[2] = { ch, 0 };
        screen_draw_text(c, r, s);
    }
}

static void redraw_all()
{
    screen_clear();
    for (int r = 0; r < TERM_ROWS; r++) {
        redraw_row(r);
    }
    prev_cur_row = cur_row;
    prev_cur_col = cur_col;
}

static void refresh_cursor()
{
    if (pager_active) {
        return;
    }
    if (prev_cur_row == cur_row && prev_cur_col == cur_col) {
        redraw_row(cur_row);
        return;
    }
    redraw_row(prev_cur_row);
    redraw_row(cur_row);
    prev_cur_row = cur_row;
    prev_cur_col = cur_col;
}

/*static void redraw()
{
    screen_clear();

    for (int r = 0; r < TERM_ROWS; r++) {

        int start_col = 0;

        // ----- PROMPT -----
        if (buffer[r][0] == '$') {
            // "$ " toujours en vert
            screen_set_color(default_prompt, default_bg);
            char p[3] = {'$', ' ', 0};
            screen_draw_text(0, r, p);
            start_col = 2;
        }

        // ----- TEXTE + CURSEUR -----
        for (int c = start_col; c < TERM_COLS; c++) {

            char ch = buffer[r][c];
            if (ch == 0) ch = ' ';

            // Curseur : inversion des couleurs
            if (r == cur_row && c == cur_col) {
                screen_set_color(default_bg, default_cursor);
            } else {
                screen_set_color(line_color[r], default_bg);
            }

            char s[2] = { ch, 0 };
            screen_draw_text(c, r, s);
        }
    }
}*/

static void scroll()
{
    for (int r = 1; r < TERM_ROWS; r++) {
        memcpy(buffer[r - 1], buffer[r], TERM_COLS + 1);
        line_color[r - 1] = line_color[r];
    }

    memset(buffer[TERM_ROWS - 1], ' ', TERM_COLS);
    buffer[TERM_ROWS - 1][TERM_COLS] = 0;
    line_color[TERM_ROWS - 1] = default_fg;
    cur_row = TERM_ROWS - 1;
    cur_col = 0;
    redraw_all();
}

static void clear_buffer()
{
    for (int r = 0; r < TERM_ROWS; r++) {
        memset(buffer[r], ' ', TERM_COLS);
        buffer[r][TERM_COLS] = 0;
        line_color[r] = default_fg;
    }
    cur_row = 0;
    cur_col = 0;
    prev_cur_row = 0;
    prev_cur_col = 0;
}

static void clear_input_rows()
{
    for (int r = 0; r < input_rows; r++) {
        int row = input_row + r;
        if (row < 0 || row >= TERM_ROWS) {
            continue;
        }
        memset(buffer[row], ' ', TERM_COLS);
        buffer[row][TERM_COLS] = 0;
        line_color[row] = default_fg;
    }
}

static void redraw_input_region(int start_row, int count)
{
    if (count <= 0) {
        return;
    }
    int end = start_row + count;
    if (start_row < 0) start_row = 0;
    if (end > TERM_ROWS) end = TERM_ROWS;
    for (int r = start_row; r < end; r++) {
        redraw_row(r);
    }
}

static void pager_render_page()
{
    clear_buffer();

    for (int r = 0; r < TERM_ROWS - 1; r++) {
        int idx = pager_index + r;
        if (idx >= (int)pager_lines.size()) {
            break;
        }

        const std::string& line = pager_lines[idx];
        int len = (int)line.size();
        if (len > TERM_COLS) len = TERM_COLS;
        for (int c = 0; c < len; c++) {
            buffer[r][c] = line[c];
        }
    }

    bool has_more = (pager_index + (TERM_ROWS - 1)) < (int)pager_lines.size();
    if (has_more) {
        const char* more = "--More--";
        int len = (int)strlen(more);
        if (len > TERM_COLS) len = TERM_COLS;
        for (int c = 0; c < len; c++) {
            buffer[TERM_ROWS - 1][c] = more[c];
        }
    }

    redraw_all();
}

static void pager_advance()
{
    if (!pager_active) {
        return;
    }

    pager_index += (TERM_ROWS - 1);
    if (pager_index >= (int)pager_lines.size()) {
        pager_active = false;
        pager_lines.clear();
        pager_index = 0;
        clear_buffer();
        screen_clear();
        term_prompt();
        return;
    }

    pager_render_page();
}

static void history_load()
{
    history.clear();
    if (!fs_sd_mounted()) {
        return;
    }

    FILE* f = fopen(history_real_path, "r");
    if (!f) {
        return;
    }

    std::string line;
    int ch = 0;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') {
            if (!line.empty()) {
                history.push_back(line);
            }
            line.clear();
        } else if (ch != '\r') {
            if (ch >= 32 && ch <= 126) {
                line.push_back((char)ch);
            }
        }
    }
    if (!line.empty()) {
        history.push_back(line);
    }
    fclose(f);

    if (history.size() > kHistoryMaxLines) {
        history.erase(history.begin(),
            history.begin() + (history.size() - kHistoryMaxLines));
        if (fs_sd_mounted()) {
            FILE* wf = fopen(history_real_path, "w");
            if (wf) {
                for (const auto& entry : history) {
                    fputs(entry.c_str(), wf);
                    fputc('\n', wf);
                }
                fclose(wf);
            }
        }
    }
}

static void history_append(const std::string& line)
{
    if (line.empty()) {
        return;
    }

    history.push_back(line);
    bool trimmed = false;
    if (history.size() > kHistoryMaxLines) {
        history.erase(history.begin(), history.begin() + 1);
        trimmed = true;
    }

    if (!fs_sd_mounted()) {
        return;
    }

    if (trimmed) {
        FILE* f = fopen(history_real_path, "w");
        if (!f) {
            return;
        }
        for (const auto& entry : history) {
            fputs(entry.c_str(), f);
            fputc('\n', f);
        }
        fclose(f);
    } else {
        FILE* f = fopen(history_real_path, "a");
        if (!f) {
            return;
        }
        fputs(line.c_str(), f);
        fputc('\n', f);
        fclose(f);
    }
}

static void set_input_line(const std::string& text)
{
    int old_row = input_row;
    int old_rows = input_rows;
    int first_width = TERM_COLS - 2;
    int len = (int)text.size();
    int remaining = (len > first_width) ? (len - first_width) : 0;
    int wrap_rows = 1 + ((remaining > 0) ? ((remaining + TERM_COLS - 1) / TERM_COLS) : 0);
    bool cursor_extra = (remaining > 0 && (remaining % TERM_COLS) == 0);
    int total_rows = wrap_rows + (cursor_extra ? 1 : 0);

    while (input_row + total_rows > TERM_ROWS) {
        scroll();
        if (input_row > 0) {
            input_row--;
        }
    }

    clear_input_rows();

    if (input_row < 0) input_row = 0;
    if (input_row >= TERM_ROWS) input_row = TERM_ROWS - 1;

    buffer[input_row][0] = '$';
    buffer[input_row][1] = ' ';

    int row = input_row;
    int col = 2;
    for (int i = 0; i < len; i++) {
        buffer[row][col] = text[(size_t)i];
        col++;
        if (col >= TERM_COLS) {
            row++;
            col = 0;
            if (row >= TERM_ROWS) {
                scroll();
                row = TERM_ROWS - 1;
                if (input_row > 0) {
                    input_row--;
                }
            }
        }
    }

    input_rows = total_rows;
    current_line = text;
    if (cursor_extra) {
        cur_row = input_row + wrap_rows;
        cur_col = 0;
    } else {
        cur_row = row;
        cur_col = col;
        if (cur_row >= TERM_ROWS) cur_row = TERM_ROWS - 1;
        if (cur_col >= TERM_COLS) cur_col = TERM_COLS - 1;
    }
    int new_end = input_row + input_rows;
    int old_end = old_row + old_rows;
    int region_start = (old_row < input_row) ? old_row : input_row;
    int region_end = (old_end > new_end) ? old_end : new_end;
    redraw_input_region(region_start, region_end - region_start);
    prev_cur_row = cur_row;
    prev_cur_col = cur_col;
}

// ------------------------------------------------------------
// Initialisation
// ------------------------------------------------------------

void term_init()
{
    default_fg = TFT_DARKGRAY;
    current_fg = default_fg;

    clear_buffer();
    current_line.clear();
    history_index = -1;
    history_saved_line.clear();
    history_load();

    redraw_all();
}

// ------------------------------------------------------------
// SORTIE TEXTE (jamais de logique d'entrée ici)
// ------------------------------------------------------------

void term_putc(char c)
{
    if (capture_active) {
        capture_buffer.push_back(c);
        return;
    }

    if (c == '\n') {
        cur_row++;
        cur_col = 0;
        if (cur_row >= TERM_ROWS) scroll();
        line_color[cur_row] = current_fg;
        refresh_cursor();
        return;
    }

    buffer[cur_row][cur_col++] = c;

    if (cur_col >= TERM_COLS) {
        cur_row++;
        cur_col = 0;
        if (cur_row >= TERM_ROWS) scroll();
        line_color[cur_row] = current_fg;
    }

    refresh_cursor();
}

void term_puts(const char *s)
{
    while (*s) {
        term_putc(*s++);
    }
}

// ------------------------------------------------------------
// ENTRÉE UTILISATEUR
// ------------------------------------------------------------

void term_input_char(char c)
{
    if (pager_active) {
        if (c == 'q' || c == 'Q') {
            term_pager_cancel();
            return;
        }
        pager_advance();
        return;
    }

    current_line.push_back(c);
    set_input_line(current_line);
    history_index = -1;
    history_saved_line.clear();
}

void term_backspace()
{
    if (pager_active) {
        pager_advance();
        return;
    }

    if (current_line.empty()) {
        return;
    }

    current_line.pop_back();
    history_index = -1;
    history_saved_line.clear();
    set_input_line(current_line);
}

void term_enter()
{
    if (pager_active) {
        pager_advance();
        return;
    }

    term_putc('\n');

    command_exec(current_line.c_str());
    if (!current_line.empty()) {
        history_append(current_line);
    }

    current_line.clear();
    history_index = -1;
    history_saved_line.clear();
    if (!editor_is_active() && !pager_active) {
        term_prompt();
    }
}

void term_pager_cancel()
{
    if (!pager_active) {
        return;
    }
    pager_active = false;
    pager_lines.clear();
    pager_index = 0;
    clear_buffer();
    screen_clear();
    term_prompt();
}

void term_cancel_input()
{
    if (pager_active || editor_is_active()) {
        return;
    }
    if (buffer[cur_row][0] != '$') {
        term_prompt();
    }
    current_line.clear();
    history_index = -1;
    history_saved_line.clear();
    set_input_line("");
}

// ------------------------------------------------------------
// Prompt / erreurs
// ------------------------------------------------------------

void term_prompt()
{
    current_fg = default_fg;
    line_color[cur_row] = current_fg;
    buffer[cur_row][0] = '$';
    buffer[cur_row][1] = ' ';
    cur_col = 2;
    input_row = cur_row;
    input_rows = 1;
    refresh_cursor();
}

void term_error(const char* msg)
{
    current_fg = default_error;
    line_color[cur_row] = current_fg;

    term_puts("error: ");
    term_puts(msg);
    term_putc('\n');

    current_fg = default_fg;
}

// ------------------------------------------------------------
// Touches spéciales (stubs propres)
// ------------------------------------------------------------

void term_escape()        { Serial.println("Esc typed"); }
static bool starts_with(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() &&
        s.compare(0, prefix.size(), prefix) == 0;
}

static std::string common_prefix(const std::vector<FsEntry>& entries)
{
    if (entries.empty()) {
        return "";
    }

    std::string prefix = entries[0].name;
    for (size_t i = 1; i < entries.size(); i++) {
        const std::string& name = entries[i].name;
        size_t j = 0;
        while (j < prefix.size() && j < name.size() && prefix[j] == name[j]) {
            j++;
        }
        prefix.resize(j);
        if (prefix.empty()) {
            break;
        }
    }
    return prefix;
}

static std::string escape_spaces(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == ' ') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

static void term_show_matches(const std::vector<FsEntry>& matches,
    const std::string& restore_line)
{
    term_putc('\n');
    bool first = true;
    for (const auto& entry : matches) {
        if (!first) {
            term_putc(' ');
        }
        std::string shown = escape_spaces(entry.name);
        term_puts(shown.c_str());
        if (entry.is_dir) {
            term_putc('/');
        }
        first = false;
    }
    term_putc('\n');
    term_prompt();
    set_input_line(restore_line);
}

void term_tab()
{
    if (pager_active) {
        pager_advance();
        return;
    }

    std::string line = current_line;
    size_t last_space = line.find_last_of(' ');
    bool first_token = (last_space == std::string::npos);
    std::string prefix = first_token ? "" : line.substr(0, last_space + 1);
    std::string token = first_token ? line : line.substr(last_space + 1);

    std::string dir_part;
    std::string base = token;
    bool path_completion = !first_token;
    size_t slash = token.find_last_of('/');
    if (slash != std::string::npos) {
        path_completion = true;
        dir_part = token.substr(0, slash + 1);
        base = token.substr(slash + 1);
    }

    std::vector<FsEntry> matches;
    if (path_completion) {
        std::string list_path = dir_part.empty() ? "." : dir_part;
        std::vector<FsEntry> entries;
        if (!fs_list_entries(list_path.c_str(), entries, true)) {
            return;
        }
        for (const auto& entry : entries) {
            if (starts_with(entry.name, base)) {
                matches.push_back(entry);
            }
        }
    } else {
        auto add_matches = [&](const char* path) {
            std::vector<FsEntry> entries;
            if (!fs_list_entries(path, entries, true)) {
                return;
            }
            for (const auto& entry : entries) {
                if (!starts_with(entry.name, base)) {
                    continue;
                }
                bool exists = false;
                for (const auto& m : matches) {
                    if (m.name == entry.name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    matches.push_back(entry);
                }
            }
        };

        add_matches("/bin");
        add_matches(".");
    }

    if (matches.empty()) {
        return;
    }

    std::string common = common_prefix(matches);
    if (common.size() > base.size()) {
        std::string new_token = escape_spaces(dir_part + common);
        current_line = prefix + new_token;
        set_input_line(current_line);
        return;
    }

    if (matches.size() == 1) {
        std::string new_token = escape_spaces(dir_part + matches[0].name);
        if (matches[0].is_dir) {
            new_token.push_back('/');
        }
        current_line = prefix + new_token;
        set_input_line(current_line);
        return;
    }

    term_show_matches(matches, current_line);
}
void term_delete()        { Serial.println("Del typed"); }
void term_cursor_up()
{
    if (pager_active) {
        pager_advance();
        return;
    }

    if (history.empty()) {
        return;
    }

    if (history_index == -1) {
        history_saved_line = current_line;
        history_index = (int)history.size() - 1;
    } else if (history_index > 0) {
        history_index--;
    }

    set_input_line(history[history_index]);
}

void term_cursor_down()
{
    if (pager_active) {
        pager_advance();
        return;
    }

    if (history.empty() || history_index == -1) {
        return;
    }

    history_index++;
    if (history_index >= (int)history.size()) {
        history_index = -1;
        set_input_line(history_saved_line);
        history_saved_line.clear();
        return;
    }

    set_input_line(history[history_index]);
}

void term_cursor_left()   { Serial.println("Left typed"); }
void term_cursor_right()  { Serial.println("Right typed"); }

void term_capture_start()
{
    capture_active = true;
    capture_buffer.clear();
}

void term_capture_stop()
{
    capture_active = false;
}

const std::string& term_capture_buffer()
{
    return capture_buffer;
}

void term_raw_input_begin()
{
    raw_input_active = true;
    raw_start_row = cur_row;
    raw_start_col = cur_col;
}

void term_raw_input_end()
{
    raw_input_active = false;
}

void term_raw_input_char(char c)
{
    term_putc(c);
}

void term_raw_input_backspace()
{
    if (!raw_input_active) {
        return;
    }
    if (cur_row < raw_start_row) {
        return;
    }
    if (cur_row == raw_start_row && cur_col <= raw_start_col) {
        return;
    }
    if (cur_col == 0) {
        cur_row--;
        cur_col = TERM_COLS;
    }
    cur_col--;
    buffer[cur_row][cur_col] = ' ';
    refresh_cursor();
}

void term_pager_start(const std::string& text)
{
    pager_lines.clear();
    pager_index = 0;
    pager_active = true;

    std::string line;
    for (char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            pager_lines.push_back(line);
            line.clear();
            continue;
        }
        line.push_back(ch);
        if ((int)line.size() >= TERM_COLS) {
            pager_lines.push_back(line);
            line.clear();
        }
    }
    pager_lines.push_back(line);

    pager_render_page();
}

bool term_pager_active()
{
    return pager_active;
}

void term_redraw()
{
    redraw_all();
}
