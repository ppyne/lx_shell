#include "keyboard.h"

#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Arduino.h>
#include <string.h>

#include "terminal.h"
#include "editor/editor.h"
#include "ui/encoding.h"

// ------------------------------------------------------------
// Utilitaires
// ------------------------------------------------------------

struct OptMapEntry {
    char key;
    const char* seq;
    uint8_t idx;
};

static OptMapEntry opt_map[] = {
    {'a', "àâæÆáäÄåÅªαa", 0},
    {'d', "δd", 0},
    {'e', "éÉèêëεe", 0},
    {'f', "ƒφΦf", 0},
    {'g', "Γg", 0},
    {'y', "ÿ¥y", 0},
    {'u', "ùûüÜú∩u", 0},
    {'i', "îïìí∞i", 0},
    {'o', "ôöÖòóº°Ωo", 0},
    {'c', "çÇ¢c", 0},
    {'l', "£∟l", 0},
    {'p', "₧π¶p", 0},
    {'s', "ßσΣ§s", 0},
    {'m', "µm", 0},
    {'n', "ñÑⁿ∩n", 0},
    {'t', "τΘt", 0},
    {'v', "√♥♦♣♠v", 0},
    {'/', "¿?►→/", 0},
    {'1', "¡‼½¼♪♫↕↨1", 0},
    {'\'', "«»↔'", 0},
    {',', "≤<◄←,", 0},
    {'.', "≥>▼↓•◘∙·■.", 0},
    {'=', "±≡≈÷=", 0},
    {'_', "±+▬_", 0},
    {';', "▲↑;", 0},
    {'8', "÷∞8", 0},
    {'0', "°○◙☺︎☻☼♂♀0", 0},
    {'2', "²2", 0}
};

static bool opt_comp_active = false;
static char opt_comp_key = 0;
static bool opt_was_down = false;

static int utf8_char_len(unsigned char c)
{
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_count_chars(const char* s)
{
    if (!s) return 0;
    int count = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        int len = utf8_char_len(*p);
        p += len;
        count++;
    }
    return count;
}

static bool utf8_nth_char(const char* s, int index, char* out, size_t out_sz)
{
    if (!s || !out || out_sz == 0 || index < 0) return false;
    const unsigned char* p = (const unsigned char*)s;
    int cur = 0;
    while (*p) {
        int len = utf8_char_len(*p);
        if (cur == index) {
            if ((size_t)len + 1 > out_sz) return false;
            memcpy(out, p, (size_t)len);
            out[len] = '\0';
            return true;
        }
        p += len;
        cur++;
    }
    return false;
}

static OptMapEntry* opt_find_entry(char c)
{
    for (size_t i = 0; i < sizeof(opt_map) / sizeof(opt_map[0]); i++) {
        if (opt_map[i].key == c) {
            return &opt_map[i];
        }
    }
    return nullptr;
}

static bool opt_utf8_to_cp437(const char* utf8, uint8_t* out)
{
    if (!utf8 || !out) return false;
    char buf[4];
    size_t n = utf8_to_cp437(utf8, buf, sizeof(buf));
    if (n == 0) return false;
    *out = (uint8_t)buf[0];
    return true;
}

static void opt_emit_cp437(uint8_t cp)
{
    if (editor_is_active()) {
        editor_handle_char_raw(cp);
    } else {
        term_input_char((char)cp);
    }
}

static void opt_emit_replace(uint8_t cp)
{
    if (editor_is_active()) {
        editor_handle_backspace();
    } else {
        term_backspace();
    }
    opt_emit_cp437(cp);
}

static uint32_t last_activity_ms = 0;

static void debug_key_event(const char* label, int code)
{
    Serial.printf("[key] %s: %d (0x%02X)\n", label, code, code & 0xFF);
}

static void debug_modifier_press(const char* label, bool current, bool* prev)
{
    if (current && !*prev) {
        debug_key_event(label, 1);
    }
    *prev = current;
}

static bool prev_fn = false;
static bool prev_shift = false;
static bool prev_ctrl = false;
static bool prev_opt = false;
static bool prev_alt = false;
static bool prev_caps = false;
static bool input_enabled = true;
static constexpr uint32_t KEY_DEBOUNCE_MS = 35;
static uint32_t last_key_ms[256] = {0};

static bool key_debounce_allow(uint8_t code)
{
    uint32_t now = millis();
    uint32_t last = last_key_ms[code];
    if (last != 0 && (uint32_t)(now - last) < KEY_DEBOUNCE_MS) {
        return false;
    }
    last_key_ms[code] = now;
    return true;
}

static bool is_modifier(uint8_t k)
{
    switch (k) {
        case KEY_FN:
        case KEY_LEFT_SHIFT:
        case KEY_LEFT_CTRL:
        case KEY_LEFT_ALT:
        case KEY_OPT:
            return true;
        default:
            return false;
    }
}

// ------------------------------------------------------------
// Poll clavier
// ------------------------------------------------------------

void keyboard_poll()
{
    // 1) Scan matériel (M5.update déjà fait dans loop)
    M5Cardputer.Keyboard.updateKeyList();

    // 2) Changement ?
    if (!M5Cardputer.Keyboard.isChange()) {
        return;
    }
    last_activity_ms = millis();

    // 3) État logique
    M5Cardputer.Keyboard.updateKeysState();
    auto &st = M5Cardputer.Keyboard.keysState();
    bool caps = M5Cardputer.Keyboard.capslocked();

    debug_modifier_press("fn", st.fn, &prev_fn);
    debug_modifier_press("shift", st.shift, &prev_shift);
    debug_modifier_press("ctrl", st.ctrl, &prev_ctrl);
    debug_modifier_press("opt", st.opt, &prev_opt);
    debug_modifier_press("alt", st.alt, &prev_alt);
    if (caps != prev_caps) {
        debug_key_event("caps", caps ? 1 : 0);
        prev_caps = caps;
    }
    if (!st.opt && opt_was_down) {
        opt_comp_active = false;
        opt_comp_key = 0;
    }
    opt_was_down = st.opt;

    if (!input_enabled) {
        return;
    }

    if (st.ctrl) {
        for (char c : st.word) {
            if (!key_debounce_allow((uint8_t)c)) {
                continue;
            }
            if (c == 'c' || c == 'C') {
                debug_key_event("ctrl+c", 0x03);
                if (term_pager_active()) {
                    term_pager_cancel();
                    return;
                }
                if (!editor_is_active()) {
                    term_cancel_input();
                    return;
                }
            }
        }
    }

    // ------------------------------------------------------------
    // FN : navigation / contrôle
    // ------------------------------------------------------------
    if (st.fn) {

        for (char c : st.word) {
            if (!key_debounce_allow((uint8_t)c)) {
                continue;
            }
            switch (c) {
                case '`':
                case '~':
                    debug_key_event("fn+esc", c);
                    if (editor_is_active()) editor_handle_escape();
                    else term_escape();
                    return;
                case ';':
                case ':':
                    debug_key_event("fn+up", c);
                    if (editor_is_active()) editor_cursor_up();
                    else term_cursor_up();
                    return;
                case '.':
                case '>':
                    debug_key_event("fn+down", c);
                    if (editor_is_active()) editor_cursor_down();
                    else term_cursor_down();
                    return;
                case ',':
                case '<':
                    debug_key_event("fn+left", c);
                    if (editor_is_active()) editor_cursor_left();
                    else term_cursor_left();
                    return;
                case '/':
                case '?':
                    debug_key_event("fn+right", c);
                    if (editor_is_active()) editor_cursor_right();
                    else term_cursor_right();
                    return;
            }
        }

        if (st.del) {
            debug_key_event("fn+backspace", 0x08);
            if (editor_is_active()) editor_handle_delete();
            else term_delete();
        }

        return; // FN ne produit jamais de texte
    }

    // ------------------------------------------------------------
    // 1️⃣ Caractères imprimables (PRIORITÉ)
    // ------------------------------------------------------------
    for (char c : st.word) {

        // Correctif Cardputer ADV : inversion - / _
        if (c == '-') c = '_';
        else if (c == '_') c = '-';

        if (!key_debounce_allow((uint8_t)c)) {
            continue;
        }

        if (st.opt) {
            OptMapEntry* entry = opt_find_entry(c);
            if (entry) {
                int count = utf8_count_chars(entry->seq);
                if (count > 0) {
                    if (entry->idx >= (uint8_t)count) {
                        entry->idx = 0;
                    }
                    char buf[8];
                    if (utf8_nth_char(entry->seq, entry->idx, buf, sizeof(buf))) {
                        uint8_t cp = 0;
                        if (opt_utf8_to_cp437(buf, &cp)) {
                            if (opt_comp_active && opt_comp_key == c) {
                                opt_emit_replace(cp);
                            } else {
                                opt_emit_cp437(cp);
                                opt_comp_active = true;
                                opt_comp_key = c;
                            }
                        }
                        entry->idx = (uint8_t)((entry->idx + 1) % count);
                        continue;
                    }
                }
            }
        }

        if (c >= 32 && c <= 126) {
            debug_key_event("char", c);
            if (editor_is_active()) editor_handle_char(c);
            else term_input_char(c);
        }
    }

    // ------------------------------------------------------------
    // 2️⃣ Touches de contrôle
    // ------------------------------------------------------------
    if (st.del) {
        if (!key_debounce_allow(0x08)) {
            return;
        }
        debug_key_event("backspace", 0x08);
        if (editor_is_active()) editor_handle_backspace();
        else term_backspace();
        return;
    }

    if (st.enter) {
        if (!key_debounce_allow(0x0d)) {
            return;
        }
        debug_key_event("enter", '\n');
        if (editor_is_active()) editor_handle_enter();
        else term_enter();
        return;
    }

    if (st.tab) {
        if (!key_debounce_allow(0x09)) {
            return;
        }
        debug_key_event("tab", '\t');
        if (editor_is_active()) editor_handle_char_raw('\t');
        else term_tab();
        return;
    }
}

// ------------------------------------------------------------
// Init clavier
// ------------------------------------------------------------

void keyboard_init()
{
    M5Cardputer.Keyboard.begin();
    last_activity_ms = millis();
}

uint32_t keyboard_last_activity_ms()
{
    return last_activity_ms;
}

void keyboard_set_input_enabled(bool enabled)
{
    input_enabled = enabled;
}
