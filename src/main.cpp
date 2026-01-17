#include <Arduino.h>
#include <M5Unified.h>
#include <M5Cardputer.h>

#include "ui/screen.h"
#include "ui/terminal.h"
#include "ui/keyboard.h"
#include "ui/screensaver.h"
#include "fs/fs.h"
#include "editor/editor.h"
#include "core/settings.h"

namespace {
static const uint32_t kSaverFrameMs = 60;
static const uint8_t kSaverStars = 40;
static const uint16_t kStarColors[] = {
    TFT_NAVY, TFT_DARKGREEN, TFT_DARKCYAN, TFT_MAROON, TFT_PURPLE, TFT_OLIVE,
    TFT_LIGHTGREY, TFT_LIGHTGRAY, TFT_DARKGREY, TFT_DARKGRAY, TFT_BLUE,
    TFT_GREEN, TFT_CYAN, TFT_RED, TFT_MAGENTA, TFT_YELLOW, TFT_WHITE,
    TFT_ORANGE, TFT_GREENYELLOW, TFT_PINK, TFT_BROWN, TFT_GOLD, TFT_SILVER,
    TFT_SKYBLUE, TFT_VIOLET
};
static const uint8_t kStarColorCount =
    sizeof(kStarColors) / sizeof(kStarColors[0]);

struct Star {
    int16_t x;
    int16_t y;
    uint8_t speed;
    uint16_t color;
};

static Star stars[kSaverStars];
static bool saver_active = false;
static bool screen_off = false;
static bool saver_suspended = false;
static uint32_t last_saver_frame_ms = 0;

static void init_stars()
{
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    for (uint8_t i = 0; i < kSaverStars; i++) {
        stars[i].x = random(w);
        stars[i].y = random(h);
        stars[i].speed = (uint8_t)random(1, 4);
        stars[i].color = kStarColors[random(kStarColorCount)];
    }
}

static void screensaver_start()
{
    saver_active = true;
    screen_off = false;
    last_saver_frame_ms = 0;
    screen_clear();
    init_stars();
}

static void screensaver_stop()
{
    saver_active = false;
    screen_off = false;
    M5.Display.setBrightness(settings_get_brightness());
    screen_clear();
    if (editor_is_active()) {
        editor_redraw();
    } else {
        term_redraw();
    }
}

static void screensaver_off()
{
    saver_active = false;
    screen_off = true;
    M5.Display.setBrightness(0);
}

static void screensaver_update(uint32_t now_ms)
{
    if (now_ms - last_saver_frame_ms < kSaverFrameMs) {
        return;
    }
    last_saver_frame_ms = now_ms;

    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    M5.Display.fillScreen(TFT_BLACK);
    for (uint8_t i = 0; i < kSaverStars; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y >= h) {
            stars[i].y = 0;
            stars[i].x = random(w);
            stars[i].speed = (uint8_t)random(1, 4);
            stars[i].color = kStarColors[random(kStarColorCount)];
        }
        M5.Display.drawPixel(stars[i].x, stars[i].y, stars[i].color);
    }
}
} // namespace

void screensaver_set_suspend(bool suspended)
{
    saver_suspended = suspended;
    if (saver_suspended && (saver_active || screen_off)) {
        screensaver_stop();
    }
}

bool screensaver_is_suspended()
{
    return saver_suspended;
}

void setup()
{
    M5.begin();          // OBLIGATOIRE, TOUJOURS EN PREMIER

    Serial.begin(115200);

    screen_init();       // initialise LovyanGFX / écran
    bool mounted = fs_mount();
    settings_init();
    M5.Display.setBrightness(settings_get_brightness());
    term_init();         // initialise le terminal
    keyboard_init();     // initialise le clavier
    //fs_init();
    editor_init();
    if (mounted) {
        term_puts("SDCard 0 mounted at /media/0\n");
    }

    term_puts("Cardputer ADV\n");
    term_puts("LX shell\n");
    /*term_puts("╔═══════════════╗\n");
    term_puts("║ Cardputer ADV ║\n");
    term_puts("╟───────────────╢\n");
    term_puts("║   LX shell    ║\n");
    term_puts("╚═══════════════╝\n");*/
    term_prompt();

    static char psram_msg[96];
    if (psramFound()) {
        snprintf(psram_msg, sizeof(psram_msg), "PSRAM: %lu bytes, free %lu bytes\n",
            (unsigned long)ESP.getPsramSize(),
            (unsigned long)ESP.getFreePsram());
    } else {
        snprintf(psram_msg, sizeof(psram_msg), "PSRAM: not found\n");
    }

    Serial.println();
    Serial.println("UART/USB console ready");
    Serial.print(psram_msg);
}

void loop()
{
    M5.update();
    M5Cardputer.update();
    keyboard_set_input_enabled(!(saver_active || screen_off));
    keyboard_poll();

    uint32_t now_ms = millis();
    uint32_t last_activity_ms = keyboard_last_activity_ms();
    uint32_t idle_ms = now_ms - last_activity_ms;

    if (saver_suspended) {
        if (saver_active || screen_off) {
            screensaver_stop();
        }
    } else {
        if ((saver_active || screen_off) && idle_ms < settings_get_saver_start_ms()) {
            screensaver_stop();
        }

        if (!screen_off && idle_ms >= settings_get_screen_off_ms()) {
            screensaver_off();
            return;
        }

        if (!saver_active && idle_ms >= settings_get_saver_start_ms()) {
            screensaver_start();
        }

        if (saver_active) {
            screensaver_update(now_ms);
        }
    }
}
