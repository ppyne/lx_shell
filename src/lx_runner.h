#pragma once

bool lx_run_script(const char* path);
bool lx_set_profile(const char* name);
const char* lx_get_profile_name();
bool lx_script_is_active();
void lx_script_request_cancel();
void lx_script_clear_cancel();
