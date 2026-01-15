#pragma once

#include <stdint.h>

void keyboard_init();
void keyboard_poll();
uint32_t keyboard_last_activity_ms();
void keyboard_set_input_enabled(bool enabled);
