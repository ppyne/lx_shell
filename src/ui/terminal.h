#pragma once

#include <string>

void term_init();
void term_putc(char c);
void term_input_char(char c);
void term_puts(const char *s);
void term_print(const char *utf8);
void term_write_bytes(const char* data, size_t len);
void term_write_bytes_error(const char* data, size_t len);
void term_prompt();
void term_error(const char* msg);
void term_enter();
void term_backspace();

void term_escape();
void term_tab();
void term_delete();

void term_cursor_up();
void term_cursor_down();
void term_cursor_left();
void term_cursor_right();

void term_capture_start();
void term_capture_stop();
const std::string& term_capture_buffer();

void term_raw_input_begin();
void term_raw_input_end();
void term_raw_input_char(char c);
void term_raw_input_backspace();

void term_pager_start(const std::string& text);
bool term_pager_active();
void term_pager_cancel();
void term_cancel_input();
void term_redraw();
