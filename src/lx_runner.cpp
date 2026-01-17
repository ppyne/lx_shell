#include "lx_runner.h"

#include <stddef.h>
#include <stdio.h>
#include <string>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <M5Cardputer.h>

#include "fs/fs.h"
#include "ui/terminal.h"
#include "ui/keyboard.h"
#include "lxsh_fs_bridge.h"
#include "lxsh_exec_bridge.h"

#include "../lib/lx/config.h"

extern "C" {
#include "ast.h"
#include "env.h"
#include "eval.h"
#include "lexer.h"
#include "lx_error.h"
#include "natives.h"
#include "parser.h"
#include "lx_ext.h"
#include "ext_lxshfs.h"
#include "ext_lxshexec.h"
#include "ext_lxshcli.h"
#include "memguard.h"
}

extern "C" {
void register_json_module(void);
void register_serializer_module(void);
void register_hex_module(void);
void register_time_module(void);
void register_utf8_module(void);
}

static void lx_output_cb(const char* data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        term_putc(data[i]);
    }
}

static void lx_report_error()
{
    const LxError* err = lx_get_error();
    if (!err) {
        term_error("lx error");
        return;
    }

    char buf[192];
    if (err->line > 0) {
        snprintf(buf, sizeof(buf), "%s (%d:%d)",
            err->message, err->line, err->col);
    } else {
        snprintf(buf, sizeof(buf), "%s", err->message);
    }
    term_error(buf);
}

enum LxProfile {
    LX_PROFILE_SAFE = 0,
    LX_PROFILE_BALANCED,
    LX_PROFILE_POWER
};

static LxProfile g_lx_profile = LX_PROFILE_POWER;

static void lx_apply_profile()
{
    size_t reserve = 0;
    switch (g_lx_profile) {
        case LX_PROFILE_SAFE:
            reserve = 120 * 1024;
            break;
        case LX_PROFILE_POWER:
            reserve = 40 * 1024;
            break;
        case LX_PROFILE_BALANCED:
        default:
            reserve = 80 * 1024;
            break;
    }
    lx_set_mem_reserve(reserve);
}

extern "C" size_t lx_platform_free_heap(void)
{
    return ESP.getFreeHeap();
}

bool lx_set_profile(const char* name)
{
    if (!name || !*name) {
        return false;
    }
    if (strcmp(name, "safe") == 0) {
        g_lx_profile = LX_PROFILE_SAFE;
    } else if (strcmp(name, "balanced") == 0) {
        g_lx_profile = LX_PROFILE_BALANCED;
    } else if (strcmp(name, "power") == 0) {
        g_lx_profile = LX_PROFILE_POWER;
    } else {
        return false;
    }
    return true;
}

const char* lx_get_profile_name()
{
    switch (g_lx_profile) {
        case LX_PROFILE_SAFE: return "safe";
        case LX_PROFILE_POWER: return "power";
        case LX_PROFILE_BALANCED:
        default:
            return "balanced";
    }
}

static bool lx_run_script_internal(const char* path)
{
    if (!path || !*path) {
        term_error("missing operand");
        return false;
    }

    std::string source;
    if (!fs_read_file(path, source)) {
        term_error("cannot read");
        return false;
    }

    lx_apply_profile();
    lx_set_output_cb(lx_output_cb);
    lx_error_clear();
    lx_reset_extensions();

    Parser parser;
    lexer_init(&parser.lexer, source.c_str(), path);
    parser.current.type = TOK_ERROR;
    parser.previous.type = TOK_ERROR;

    AstNode* program = parse_program(&parser);
    if (!program || lx_has_error()) {
        lx_report_error();
        ast_free(program);
        return false;
    }

    Env* global = env_new(NULL);
    install_stdlib();

    lxsh_fs_register();
#if defined(LX_TARGET_LXSH) && LX_TARGET_LXSH
    register_lxshfs_module();
#endif
    lxsh_exec_register();
#if defined(LX_TARGET_LXSH) && LX_TARGET_LXSH
    register_lxshexec_module();
    register_lxshcli_module();
#if LX_ENABLE_JSON
    register_json_module();
#endif
#if LX_ENABLE_SERIALIZER
    register_serializer_module();
#endif
#if LX_ENABLE_HEX
    register_hex_module();
#endif
#if LX_ENABLE_TIME
    register_time_module();
#endif
#if LX_ENABLE_UTF8
    register_utf8_module();
#endif
#endif
    lx_init_modules(global);

    EvalResult r = eval_program(program, global);
    if (lx_has_error()) {
        lx_report_error();
        value_free(r.value);
        env_free(global);
        ast_free(program);
        return false;
    }

    value_free(r.value);
    env_free(global);
    ast_free(program);
    return true;
}

struct LxTaskArgs {
    std::string path;
    TaskHandle_t caller;
    bool result;
};

static TaskHandle_t g_lx_task_handle = NULL;
#ifdef LX_TASK_STACK_SIZE
static constexpr uint32_t kLxTaskStackWords =
    (LX_TASK_STACK_SIZE + sizeof(StackType_t) - 1) / sizeof(StackType_t);
#else
static constexpr uint32_t kLxTaskStackWords =
    (32768 + sizeof(StackType_t) - 1) / sizeof(StackType_t);
#endif
static StackType_t g_lx_task_stack[kLxTaskStackWords];
static StaticTask_t g_lx_task_tcb;
static volatile bool g_lx_script_active = false;
static volatile bool g_lx_script_cancel = false;

bool lx_script_is_active()
{
    return g_lx_script_active;
}

void lx_script_request_cancel()
{
    g_lx_script_cancel = true;
    lxsh_exec_request_cancel();
}

void lx_script_clear_cancel()
{
    g_lx_script_cancel = false;
}

extern "C" int lxsh_cancel_requested(void)
{
    return g_lx_script_cancel ? 1 : 0;
}

static void lx_task_entry(void* pv)
{
    LxTaskArgs* args = static_cast<LxTaskArgs*>(pv);
    g_lx_script_active = true;
    g_lx_script_cancel = false;
    args->result = lx_run_script_internal(args->path.c_str());
    g_lx_script_active = false;
    g_lx_script_cancel = false;
    g_lx_task_handle = NULL;
    xTaskNotifyGive(args->caller);
    vTaskDelete(NULL);
}

bool lx_run_script(const char* path)
{
    if (g_lx_task_handle) {
        term_error("lx task busy");
        return false;
    }

    LxTaskArgs* args = new LxTaskArgs{std::string(path ? path : ""), xTaskGetCurrentTaskHandle(), false};
    if (!args) {
        term_error("no memory");
        return false;
    }

    g_lx_task_handle = xTaskCreateStatic(
        lx_task_entry,
        "lx_task",
        kLxTaskStackWords,
        args,
        1,
        g_lx_task_stack,
        &g_lx_task_tcb);
    if (!g_lx_task_handle) {
        delete args;
        term_error("lx task failed");
        return false;
    }

    while (ulTaskNotifyTake(pdTRUE, 0) == 0) {
        M5.update();
        M5Cardputer.update();
        keyboard_poll();
        vTaskDelay(1);
    }
    bool result = args->result;
    delete args;
    return result;
}
