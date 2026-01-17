#include "lxsh_exec_bridge.h"

#include "core/command.h"
#include "lxsh_exec.h"
#include "ui/terminal.h"

#include <stdlib.h>
#include <string.h>

static volatile bool g_lxsh_exec_active = false;
static volatile bool g_lxsh_exec_cancel = false;

void lxsh_exec_set_active(bool active)
{
    g_lxsh_exec_active = active;
}

bool lxsh_exec_is_active()
{
    return g_lxsh_exec_active;
}

void lxsh_exec_request_cancel()
{
    g_lxsh_exec_cancel = true;
}

bool lxsh_exec_cancel_requested()
{
    return g_lxsh_exec_cancel;
}

void lxsh_exec_clear_cancel()
{
    g_lxsh_exec_cancel = false;
}

static int lxsh_exec_command(const char* cmd)
{
    if (!cmd || !*cmd) {
        return 1;
    }
    lxsh_exec_set_active(true);
    lxsh_exec_clear_cancel();
    int status = command_exec(cmd) ? 0 : 1;
    lxsh_exec_set_active(false);
    lxsh_exec_clear_cancel();
    return status;
}

static int lxsh_exec_capture(const char* cmd, char** out_data, size_t* out_len)
{
    if (!out_data || !out_len) {
        return 1;
    }
    *out_data = NULL;
    *out_len = 0;

    if (!cmd || !*cmd) {
        return 1;
    }

    lxsh_exec_set_active(true);
    lxsh_exec_clear_cancel();
    term_capture_start();
    int status = command_exec(cmd) ? 0 : 1;
    term_capture_stop();
    lxsh_exec_set_active(false);
    lxsh_exec_clear_cancel();

    const std::string& buf = term_capture_buffer();
    size_t len = buf.size();
    char* data = static_cast<char*>(malloc(len + 1));
    if (!data) {
        return 1;
    }
    memcpy(data, buf.data(), len);
    data[len] = '\0';
    *out_data = data;
    *out_len = len;
    return status;
}

void lxsh_exec_register()
{
    static const LxShExecOps ops = {
        lxsh_exec_command,
        lxsh_exec_capture
    };
    lxsh_set_exec_ops(&ops);
}
