#include "screen.h"
#include <M5Unified.h>
#include "Bm437_ATT_PC6300.h"
#include <string.h>

// Mesures empiriques pour FreeMonoBold9pt7b
#define CHAR_W 8
#define CHAR_H 16

void screen_init() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.clear(TFT_BLACK);
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setFont(&Bm437_ATT_PC6300_16pt8b);
  M5.Display.cp437(true);
  M5.Display.setTextSize(1);
  
  M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);

}

void screen_clear() {
  M5.Display.clear(TFT_BLACK);
}

void screen_draw_text(int col, int row, const char *s) {
  int x = col * CHAR_W;
  int y = row * CHAR_H;
  if (s && s[0] && s[1] == '\0') {
    M5.Display.drawChar((uint8_t)s[0], x, y);
  } else {
    M5.Display.drawString(s, x, y);
  }
}

void screen_set_color(uint16_t fg, uint16_t bg) {
  M5.Display.setTextColor(fg, bg);
}

void screen_draw_text_direct(int col, int row, const char *s) {
  int x = col * CHAR_W;
  int y = row * CHAR_H;
  if (s && s[0] && s[1] == '\0') {
    M5.Display.drawChar((uint8_t)s[0], x, y);
  } else {
    M5.Display.drawString(s, x, y);
  }
}
