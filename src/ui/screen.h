#pragma once
#include <stdint.h>

void screen_init();
void screen_clear();
void screen_draw_text(int col, int row, const char *s);
void screen_set_color(uint16_t fg, uint16_t bg);
void screen_draw_text_direct(int col, int row, const char *s);
