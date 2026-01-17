#pragma once

void lxsh_exec_register();
void lxsh_exec_set_active(bool active);
bool lxsh_exec_is_active();
void lxsh_exec_request_cancel();
bool lxsh_exec_cancel_requested();
void lxsh_exec_clear_cancel();
