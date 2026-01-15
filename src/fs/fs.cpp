#include "fs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <algorithm>
#include <time.h>
#include "ff.h"

#include "ui/terminal.h"
#include "hal/sdcard.h"

// ------------------------------------------------------------
// État global
// ------------------------------------------------------------

static char cwd[128] = "/";

// ------------------------------------------------------------
// Utils
// ------------------------------------------------------------

static bool path_eq(const char* a, const char* b)
{
    return strcmp(a, b) == 0;
}

// construit un chemin canonique absolu à partir de cwd + path
static bool fs_norm(const char* base, const char* path, char* out, size_t out_sz)
{
    if (!path || !*path) {
        strncpy(out, base, out_sz);
        return true;
    }

    char tmp[128];

    if (path[0] == '/') {
        strncpy(tmp, path, sizeof(tmp));
    } else {
        if (strcmp(base, "/") == 0)
            snprintf(tmp, sizeof(tmp), "/%s", path);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", base, path);
    }

    // normalisation . et ..
    char* parts[16];
    int n = 0;

    char buf[128];
    strncpy(buf, tmp, sizeof(buf));

    char* tok = strtok(buf, "/");
    while (tok && n < 16) {
        if (strcmp(tok, ".") == 0) {
            // ignore
        } else if (strcmp(tok, "..") == 0) {
            if (n > 0) n--;
        } else {
            parts[n++] = tok;
        }
        tok = strtok(nullptr, "/");
    }

    // reconstruction
    out[0] = '/';
    out[1] = '\0';
    for (int i = 0; i < n; i++) {
        strncat(out, parts[i], out_sz);
        if (i != n - 1)
            strncat(out, "/", out_sz);
    }

    return true;
}

static bool fs_resolve_media_path(const char* path, char* out, size_t out_sz)
{
    if (!path || !*path) {
        return false;
    }

    if (!sd_is_mounted()) {
        return false;
    }

    char canon[128];
    fs_norm(cwd, path, canon, sizeof(canon));

    if (path_eq(canon, "/media/0") || path_eq(canon, "/media/0/.")) {
        strncpy(out, "/sdcard", out_sz);
        return true;
    }

    if (strncmp(canon, "/media/0/", 9) == 0) {
        snprintf(out, out_sz, "/sdcard/%s", canon + 9);
        return true;
    }

    return false;
}

static bool fs_real_to_virtual(const char* real, char* out, size_t out_sz)
{
    const char* prefix = "/sdcard";
    size_t prefix_len = strlen(prefix);

    if (strncmp(real, prefix, prefix_len) != 0) {
        return false;
    }

    if (strcmp(real, "/sdcard") == 0) {
        strncpy(out, "/media/0", out_sz);
        return true;
    }

    if (real[prefix_len] == '/') {
        snprintf(out, out_sz, "/media/0%s", real + prefix_len);
        return true;
    }

    return false;
}

static bool match_pattern(const char* name, const char* pattern, bool ci)
{
    if (!pattern || !*pattern) {
        return true;
    }
    if (!name) {
        return false;
    }

    char c = *pattern;
    if (c == '*') {
        while (*pattern == '*') pattern++;
        if (!*pattern) {
            return true;
        }
        for (const char* p = name; *p; p++) {
            if (match_pattern(p, pattern, ci)) {
                return true;
            }
        }
        return false;
    }

    if (c == '?') {
        if (!*name) {
            return false;
        }
        return match_pattern(name + 1, pattern + 1, ci);
    }

    if (!*name) {
        return false;
    }

    char a = *name;
    char b = c;
    if (ci) {
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
    }
    if (a != b) {
        return false;
    }
    return match_pattern(name + 1, pattern + 1, ci);
}

static const char* fs_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool find_walk(const char* real_root, const char* pattern, bool ci, bool include_self)
{
    if (include_self) {
        char virt[192];
        if (fs_real_to_virtual(real_root, virt, sizeof(virt))) {
            const char* name = nullptr;
            if (strcmp(real_root, "/sdcard") == 0) {
                name = "0";
            } else {
                name = fs_basename(real_root);
            }
            if (!pattern || match_pattern(name, pattern, ci)) {
                term_puts(virt);
                term_putc('\n');
            }
        }
    }

    DIR* dir = opendir(real_root);
    if (!dir) {
        return false;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char full[192];
        snprintf(full, sizeof(full), "%s/%s", real_root, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }

        if (!pattern || match_pattern(ent->d_name, pattern, ci)) {
            char virt[192];
            if (fs_real_to_virtual(full, virt, sizeof(virt))) {
                term_puts(virt);
                term_putc('\n');
            }
        }

        if (S_ISDIR(st.st_mode)) {
            find_walk(full, pattern, ci, false);
        }
    }

    closedir(dir);
    return true;
}

// ------------------------------------------------------------
// État
// ------------------------------------------------------------

bool fs_sd_mounted()
{
    return sd_is_mounted();
}

const char* fs_pwd()
{
    return cwd;
}

// ------------------------------------------------------------
// cd
// ------------------------------------------------------------

bool fs_cd(const char* path)
{
    char canon[128];
    fs_norm(cwd, path, canon, sizeof(canon));

    // répertoires virtuels valides
    if (path_eq(canon, "/") ||
        path_eq(canon, "/bin") ||
        path_eq(canon, "/media") ||
        path_eq(canon, "/media/0")) {

        if (path_eq(canon, "/media/0") && !sd_is_mounted())
            return false;

        strncpy(cwd, canon, sizeof(cwd));
        return true;
    }

    // sous-répertoires SD
    if (strncmp(canon, "/media/0/", 9) == 0) {
        if (!sd_is_mounted())
            return false;

        char real[128];
        snprintf(real, sizeof(real), "/sdcard/%s", canon + 9);

        DIR* d = opendir(real);
        if (!d)
            return false;
        closedir(d);

        strncpy(cwd, canon, sizeof(cwd));
        return true;
    }

    return false;
}

// ------------------------------------------------------------
// Montage SD
// ------------------------------------------------------------

bool fs_mount()
{
    return sd_mount(false);
}

void fs_umount()
{
    sd_umount();
}

// ------------------------------------------------------------
// Listage
// ------------------------------------------------------------

static void format_size(uint32_t size, char* out, size_t out_sz)
{
    const char units[] = { 0, 'k', 'm', 'g' };
    unsigned long value = size;
    int unit_idx = 0;
    while (value >= 1024 && unit_idx < 3) {
        value /= 1024;
        unit_idx++;
    }
    if (value > 999) {
        value = 999;
    }
    if (unit_idx == 0) {
        snprintf(out, out_sz, "%lu", value);
    } else {
        snprintf(out, out_sz, "%lu%c", value, units[unit_idx]);
    }
}

static const char* k_bin_names[] = {
    "ls", "pwd", "cd", "mount", "umount",
    "mkdir", "rmdir", "cp", "mv", "rm",
    "vi", "touch", "cat", "lx", "more", "less", "find",
    "clear", "reset", "battery", "shutdown",
    "man",
    "uptime"
};

static bool bin_has(const char* name)
{
    if (!name || !*name) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_bin_names) / sizeof(k_bin_names[0]); i++) {
        if (strcmp(k_bin_names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static void list_bin_entries(std::vector<FsEntry>& out, bool include_hidden)
{
    for (size_t i = 0; i < sizeof(k_bin_names) / sizeof(k_bin_names[0]); i++) {
        if (!include_hidden && k_bin_names[i][0] == '.') {
            continue;
        }
        FsEntry e;
        e.name = k_bin_names[i];
        e.is_dir = false;
        out.push_back(e);
    }

    std::sort(out.begin(), out.end(),
        [](const FsEntry& a, const FsEntry& b) {
            return a.name < b.name;
        });
}

static void list_bin(const char* opts)
{
    bool opt_long = opts && strchr(opts, 'l');
    bool opt_all = opts && strchr(opts, 'a');
    const char* date = "1970-01-01";

    std::vector<FsEntry> entries;
    list_bin_entries(entries, opt_all);

    char line[96];
    const char* size_str = "0";
    for (const auto& e : entries) {
        if (opt_long) {
            const char perm_r = 'r';
            const char perm_h = (opt_all && e.name[0] == '.') ? 'h' : '-';
            const char perm_s = 's';
            const char perm_a = '-';
            snprintf(line, sizeof(line), "-%c%c%c%c %4s %s %s",
                perm_r, perm_h, perm_s, perm_a, size_str, date, e.name.c_str());
            term_puts(line);
            term_putc('\n');
        } else {
            term_puts(e.name.c_str());
            term_putc('\n');
        }
    }
}

struct ls_entry {
    std::string name;
    uint32_t mtime;
    uint32_t size;
    bool is_dir;
    uint8_t fat_attr;
    bool fat_valid;
};

static bool list_sd_dir(const char* real_path, const char* opts)
{
    bool opt_all = opts && strchr(opts, 'a');
    bool opt_long = opts && strchr(opts, 'l');
    bool opt_time = opts && strchr(opts, 't');
    bool opt_rev = opts && strchr(opts, 'r');

    DIR* dir = opendir(real_path);
    if (!dir)
        return false;

    std::vector<ls_entry> entries;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (!opt_all && ent->d_name[0] == '.') {
            continue;
        }

        ls_entry e;
        e.name = ent->d_name;
        e.mtime = 0;
        e.size = 0;
        e.is_dir = false;
        e.fat_attr = 0;
        e.fat_valid = false;

        char full[192];
        snprintf(full, sizeof(full), "%s/%s", real_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            e.mtime = (uint32_t)st.st_mtime;
            e.size = (uint32_t)st.st_size;
            e.is_dir = S_ISDIR(st.st_mode);
        }
        FILINFO fno;
        if (f_stat(full, &fno) == FR_OK) {
            e.fat_attr = fno.fattrib;
            e.fat_valid = true;
        }

        entries.push_back(e);
    }
    closedir(dir);

    if (opt_time) {
        std::sort(entries.begin(), entries.end(),
            [](const ls_entry& a, const ls_entry& b) {
                if (a.mtime == b.mtime) {
                    return a.name < b.name;
                }
                return a.mtime > b.mtime;
            });
    } else {
        std::sort(entries.begin(), entries.end(),
            [](const ls_entry& a, const ls_entry& b) {
                return a.name < b.name;
            });
    }

    if (opt_rev) {
        std::reverse(entries.begin(), entries.end());
    }

    char line[96];
    char size_str[8];
    for (const auto& e : entries) {
        if (opt_long) {
            const char type = e.is_dir ? 'd' : '-';
            const bool hidden_by_name = !e.name.empty() && e.name[0] == '.';
            const bool hidden_by_attr = e.fat_valid && (e.fat_attr & AM_HID);
            const bool read_only = e.fat_valid && (e.fat_attr & AM_RDO);
            const bool archive = e.fat_valid && (e.fat_attr & AM_ARC);
            const bool hidden = hidden_by_name || hidden_by_attr;
            const char perm_r = 'r';
            const char perm_w = read_only ? '-' : 'w';
            const char perm_h = hidden_by_attr ? 'H' : (hidden_by_name ? 'h' : '-');
            const char perm_a = archive ? 'a' : '-';

            char date[16] = "1970-01-01";
            time_t tt = (time_t)e.mtime;
            struct tm tm_buf;
            struct tm* tm_ptr = localtime_r(&tt, &tm_buf);
            if (tm_ptr) {
                snprintf(date, sizeof(date), "%04d-%02d-%02d",
                    tm_ptr->tm_year + 1900,
                    tm_ptr->tm_mon + 1,
                    tm_ptr->tm_mday);
            }

            format_size(e.size, size_str, sizeof(size_str));
            snprintf(line, sizeof(line), "%c%c%c%c%c %4s %s %s",
                type, perm_r, perm_w, perm_h, perm_a,
                size_str, date, e.name.c_str());
            term_puts(line);
            term_putc('\n');
        } else {
            term_puts(e.name.c_str());
            term_putc('\n');
        }
    }
    return true;
}

static bool list_sd_dir_entries(const char* real_path,
    std::vector<FsEntry>& out, bool include_hidden)
{
    DIR* dir = opendir(real_path);
    if (!dir) {
        return false;
    }

    std::vector<FsEntry> entries;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!include_hidden && ent->d_name[0] == '.') {
            continue;
        }

        FsEntry e;
        e.name = ent->d_name;
        e.is_dir = false;

        char full[192];
        snprintf(full, sizeof(full), "%s/%s", real_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            e.is_dir = S_ISDIR(st.st_mode);
        }

        entries.push_back(e);
    }
    closedir(dir);

    std::sort(entries.begin(), entries.end(),
        [](const FsEntry& a, const FsEntry& b) {
            return a.name < b.name;
        });

    out.swap(entries);
    return true;
}

bool fs_list(const char* path, const char* opts)
{
    char canon[128];
    fs_norm(cwd, path, canon, sizeof(canon));

    if (path_eq(canon, "/")) {
        term_puts("bin\n");
        term_puts("media\n");
        return true;
    }

    if (path_eq(canon, "/bin")) {
        list_bin(opts);
        return true;
    }

    if (path_eq(canon, "/media")) {
        if (sd_is_mounted())
            term_puts("0\n");
        return true;
    }

    if (path_eq(canon, "/media/0")) {
        if (!sd_is_mounted())
            return false;
        return list_sd_dir("/sdcard", opts);
    }

    if (strncmp(canon, "/media/0/", 9) == 0) {
        if (!sd_is_mounted())
            return false;

        char real[128];
        snprintf(real, sizeof(real), "/sdcard/%s", canon + 9);
        return list_sd_dir(real, opts);
    }

    return false;
}

bool fs_resolve_path(const char* path, char* out, size_t out_sz)
{
    return fs_norm(cwd, path, out, out_sz);
}

bool fs_stat(const char* path, FsStat& out)
{
    out.size = 0;
    out.is_dir = false;
    out.is_file = false;

    char canon[128];
    fs_norm(cwd, path, canon, sizeof(canon));

    if (path_eq(canon, "/") || path_eq(canon, "/bin") ||
        path_eq(canon, "/media") || path_eq(canon, "/media/0")) {
        if (path_eq(canon, "/media/0") && !sd_is_mounted()) {
            return false;
        }
        out.is_dir = true;
        return true;
    }

    if (strncmp(canon, "/bin/", 5) == 0) {
        const char* name = canon + 5;
        if (!*name) {
            return false;
        }
        if (!bin_has(name)) {
            return false;
        }
        out.is_file = true;
        return true;
    }

    if (strncmp(canon, "/media/0/", 9) == 0) {
        if (!sd_is_mounted()) {
            return false;
        }

        char real[128];
        snprintf(real, sizeof(real), "/sdcard/%s", canon + 9);
        struct stat st;
        if (stat(real, &st) != 0) {
            return false;
        }
        out.size = (uint32_t)st.st_size;
        out.is_dir = S_ISDIR(st.st_mode);
        out.is_file = S_ISREG(st.st_mode);
        return true;
    }

    return false;
}

bool fs_write_file(const char* path, const unsigned char* data, size_t len)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }

    FILE* f = fopen(real, "wb");
    if (!f) {
        return false;
    }
    size_t written = 0;
    if (len > 0) {
        written = fwrite(data, 1, len, f);
    }
    fclose(f);
    return written == len;
}

bool fs_append_file(const char* path, const unsigned char* data, size_t len)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }

    FILE* f = fopen(real, "ab");
    if (!f) {
        return false;
    }
    size_t written = 0;
    if (len > 0) {
        written = fwrite(data, 1, len, f);
    }
    fclose(f);
    return written == len;
}

bool fs_list_entries(const char* path, std::vector<FsEntry>& out,
    bool include_hidden)
{
    out.clear();

    char canon[128];
    fs_norm(cwd, path, canon, sizeof(canon));

    if (path_eq(canon, "/")) {
        out.push_back({ "bin", true });
        out.push_back({ "media", true });
        return true;
    }

    if (path_eq(canon, "/bin")) {
        list_bin_entries(out, include_hidden);
        return true;
    }

    if (path_eq(canon, "/media")) {
        if (sd_is_mounted()) {
            out.push_back({ "0", true });
        }
        return true;
    }

    if (path_eq(canon, "/media/0")) {
        if (!sd_is_mounted())
            return false;
        return list_sd_dir_entries("/sdcard", out, include_hidden);
    }

    if (strncmp(canon, "/media/0/", 9) == 0) {
        if (!sd_is_mounted())
            return false;

        char real[128];
        snprintf(real, sizeof(real), "/sdcard/%s", canon + 9);
        return list_sd_dir_entries(real, out, include_hidden);
    }

    return false;
}

bool fs_mkdir(const char* path)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }
    return mkdir(real, 0777) == 0;
}

bool fs_rmdir(const char* path)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }
    return rmdir(real) == 0;
}

bool fs_rm(const char* path)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }
    return remove(real) == 0;
}

bool fs_cp(const char* src, const char* dst)
{
    char real_src[128];
    char real_dst[128];
    if (!fs_resolve_media_path(src, real_src, sizeof(real_src))) {
        return false;
    }
    if (!fs_resolve_media_path(dst, real_dst, sizeof(real_dst))) {
        return false;
    }

    FILE* in = fopen(real_src, "r");
    if (!in) {
        return false;
    }

    FILE* out = fopen(real_dst, "w");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[256];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return false;
        }
    }

    fclose(in);
    fclose(out);
    return true;
}

bool fs_mv(const char* src, const char* dst)
{
    char real_src[128];
    char real_dst[128];
    if (!fs_resolve_media_path(src, real_src, sizeof(real_src))) {
        return false;
    }
    if (!fs_resolve_media_path(dst, real_dst, sizeof(real_dst))) {
        return false;
    }

    if (rename(real_src, real_dst) == 0) {
        return true;
    }

    if (!fs_cp(src, dst)) {
        return false;
    }

    return fs_rm(src);
}

bool fs_touch(const char* path)
{
    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }

    FILE* f = fopen(real, "a");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

bool fs_read_file(const char* path, std::string& out)
{
    out.clear();

    char real[128];
    if (!fs_resolve_media_path(path, real, sizeof(real))) {
        return false;
    }

    FILE* f = fopen(real, "rb");
    if (!f) {
        return false;
    }

    int ch = 0;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\r') {
            continue;
        }
        out.push_back((char)ch);
    }
    fclose(f);
    return true;
}

bool fs_find(const char* path, const char* pattern, bool case_insensitive)
{
    const char* in_path = (path && *path) ? path : ".";

    char canon[128];
    fs_norm(cwd, in_path, canon, sizeof(canon));

    if (path_eq(canon, "/")) {
        if (!pattern || match_pattern("bin", pattern, case_insensitive)) {
            term_puts("/bin\n");
        }
        if (!pattern || match_pattern("media", pattern, case_insensitive)) {
            term_puts("/media\n");
        }
        if (sd_is_mounted()) {
            if (!pattern || match_pattern("0", pattern, case_insensitive)) {
                term_puts("/media/0\n");
            }
            return find_walk("/sdcard", pattern, case_insensitive, false);
        }
        return true;
    }

    if (path_eq(canon, "/bin")) {
        const char* names[] = {
            "ls", "pwd", "cd", "mount", "umount",
            "mkdir", "rmdir", "cp", "mv", "rm",
            "vi", "touch", "cat", "more", "find",
            "uptime"
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (!pattern || match_pattern(names[i], pattern, case_insensitive)) {
                term_puts("/bin/");
                term_puts(names[i]);
                term_putc('\n');
            }
        }
        return true;
    }

    if (path_eq(canon, "/media")) {
        if (sd_is_mounted()) {
            if (!pattern || match_pattern("0", pattern, case_insensitive)) {
                term_puts("/media/0\n");
            }
        }
        return true;
    }

    char real[128];
    if (!fs_resolve_media_path(canon, real, sizeof(real))) {
        return false;
    }

    return find_walk(real, pattern, case_insensitive, true);
}
