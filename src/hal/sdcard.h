#pragma once
#include <stdbool.h>

bool sd_mount(bool format_if_failed);
void sd_umount();
bool sd_is_mounted();
