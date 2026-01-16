#pragma once

#include <stdint.h>

void editor_init();
bool editor_is_active();
void editor_open(const char* path);
void editor_open_with_mode(const char* path, bool nano_mode);
void editor_close();

void editor_handle_char(char c);
void editor_handle_char_raw(uint8_t c);
void editor_handle_ctrl(uint8_t c);
void editor_handle_backspace();
void editor_handle_enter();
void editor_handle_escape();
void editor_handle_delete();
void editor_redraw();

void editor_cursor_up();
void editor_cursor_down();
void editor_cursor_left();
void editor_cursor_right();
