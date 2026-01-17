#pragma once

#include <stdint.h>

void settings_init();
void settings_load_if_available();
void settings_save_if_available();
void settings_load_script_if_available();
void settings_save_script_if_available();

uint8_t settings_get_brightness();
void settings_set_brightness(uint8_t value);

uint32_t settings_get_saver_start_ms();
uint32_t settings_get_screen_off_ms();
void settings_set_saver_minutes(uint32_t minutes);
void settings_set_screen_off_minutes(uint32_t minutes);

const char* settings_get_lx_profile();
bool settings_set_lx_profile(const char* name);
