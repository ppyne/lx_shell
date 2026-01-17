#pragma once

#include <stdint.h>

void lxsh_cli_push_char(uint8_t c);

extern "C" {
int lxsh_cli_read_key(int* out_code);
char* lxsh_cli_read_line(void);
void lxsh_cli_prompt(const char* prompt);
}
