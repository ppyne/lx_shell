#pragma once
#include <stdbool.h>
#include <string>
#include <vector>
#include <stddef.h>

struct FsEntry {
    std::string name;
    bool is_dir;
};

struct FsStat {
    uint32_t size;
    bool is_dir;
    bool is_file;
};

// état
bool fs_sd_mounted();

// cwd
const char* fs_pwd();
bool fs_cd(const char* path);

// montage / démontage
bool fs_mount();
void fs_umount();

// listage
bool fs_list(const char* path, const char* opts);
bool fs_list_entries(const char* path, std::vector<FsEntry>& out,
    bool include_hidden);
bool fs_stat(const char* path, FsStat& out);
bool fs_write_file(const char* path, const unsigned char* data, size_t len);
bool fs_append_file(const char* path, const unsigned char* data, size_t len);
bool fs_resolve_path(const char* path, char* out, size_t out_sz);
bool fs_resolve_real_path(const char* path, char* out, size_t out_sz);

// fichiers / dossiers (SD)
bool fs_mkdir(const char* path);
bool fs_rmdir(const char* path);
bool fs_rm(const char* path);
bool fs_mv(const char* src, const char* dst);
bool fs_cp(const char* src, const char* dst);
bool fs_touch(const char* path);
bool fs_read_file(const char* path, std::string& out);
bool fs_find(const char* path, const char* pattern, bool case_insensitive);
