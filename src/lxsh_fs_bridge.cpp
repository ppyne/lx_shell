#include "lxsh_fs_bridge.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

#include "fs/fs.h"

extern "C" {
#include "lxsh_fs.h"
}

static int lxsh_read_file(const char* path, char** out_data, size_t* out_len,
    char** out_resolved)
{
    if (!out_data || !out_len) {
        return 0;
    }
    std::string content;
    if (!fs_read_file(path, content)) {
        return 0;
    }
    size_t len = content.size();
    char* buf = (char*)malloc(len + 1);
    if (!buf) {
        return 0;
    }
    memcpy(buf, content.data(), len);
    buf[len] = '\0';
    *out_data = buf;
    *out_len = len;

    if (out_resolved) {
        char canon[128];
        if (fs_resolve_path(path, canon, sizeof(canon))) {
            *out_resolved = strdup(canon);
        } else {
            *out_resolved = strdup(path ? path : "");
        }
    }
    return 1;
}

static int lxsh_write_file(const char* path, const unsigned char* data, size_t len)
{
    if (!path) {
        return 0;
    }
    return fs_write_file(path, data, len) ? 1 : 0;
}

static int lxsh_file_exists(const char* path)
{
    FsStat st;
    return fs_stat(path, st) ? 1 : 0;
}

static int lxsh_file_size(const char* path, size_t* out_size)
{
    if (!out_size) {
        return 0;
    }
    FsStat st;
    if (!fs_stat(path, st)) {
        return 0;
    }
    *out_size = st.size;
    return 1;
}

static int lxsh_is_dir(const char* path)
{
    FsStat st;
    if (!fs_stat(path, st)) {
        return 0;
    }
    return st.is_dir ? 1 : 0;
}

static int lxsh_is_file(const char* path)
{
    FsStat st;
    if (!fs_stat(path, st)) {
        return 0;
    }
    return st.is_file ? 1 : 0;
}

static int lxsh_mkdir(const char* path)
{
    return fs_mkdir(path) ? 1 : 0;
}

static int lxsh_rmdir(const char* path)
{
    return fs_rmdir(path) ? 1 : 0;
}

static int lxsh_unlink(const char* path)
{
    return fs_rm(path) ? 1 : 0;
}

static int lxsh_copy(const char* src, const char* dst)
{
    return fs_cp(src, dst) ? 1 : 0;
}

static int lxsh_rename(const char* src, const char* dst)
{
    return fs_mv(src, dst) ? 1 : 0;
}

static int lxsh_pwd(char* out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return 0;
    }
    const char* pwd = fs_pwd();
    if (!pwd) {
        return 0;
    }
    strncpy(out, pwd, out_sz);
    out[out_sz - 1] = '\0';
    return 1;
}

static int lxsh_list_dir(const char* path, char*** out_names, size_t* out_count)
{
    if (!out_names || !out_count) {
        return 0;
    }
    std::vector<FsEntry> entries;
    if (!fs_list_entries(path, entries, true)) {
        return 0;
    }
    std::vector<std::string> names;
    for (const auto& entry : entries) {
        if (entry.name == "." || entry.name == "..") {
            continue;
        }
        names.push_back(entry.name);
    }
    size_t count = names.size();
    char** list = (char**)malloc(sizeof(char*) * count);
    if (!list && count > 0) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        list[i] = strdup(names[i].c_str());
    }
    *out_names = list;
    *out_count = count;
    return 1;
}

static const char* lxsh_temp_dir()
{
    return "/media/0";
}

static int lxsh_tempnam(const char* prefix, char* out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return 0;
    }
    const char* pfx = (prefix && *prefix) ? prefix : "lx";
    for (int i = 0; i < 16; i++) {
        unsigned long n = (unsigned long)millis() + (unsigned long)i * 1337UL;
        snprintf(out, out_sz, "/media/0/%s%lu.lx", pfx, n);
        FsStat st;
        if (!fs_stat(out, st)) {
            if (fs_touch(out)) {
                return 1;
            }
        }
    }
    return 0;
}

void lxsh_fs_register()
{
    static LxShFsOps ops = {
        lxsh_read_file,
        lxsh_write_file,
        lxsh_file_exists,
        lxsh_file_size,
        lxsh_is_dir,
        lxsh_is_file,
        lxsh_mkdir,
        lxsh_rmdir,
        lxsh_unlink,
        lxsh_copy,
        lxsh_rename,
        lxsh_pwd,
        lxsh_list_dir,
        lxsh_temp_dir,
        lxsh_tempnam
    };

    lxsh_set_fs_ops(&ops);
}
