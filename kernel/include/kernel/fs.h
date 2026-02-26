#ifndef WALU_FS_H
#define WALU_FS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FS_OK = 0,
    FS_ERR_NOT_FOUND,
    FS_ERR_EXISTS,
    FS_ERR_NOT_DIR,
    FS_ERR_IS_DIR,
    FS_ERR_INVALID,
    FS_ERR_NO_SPACE,
} fs_status_t;

typedef struct {
    char name[32];
    bool is_dir;
    size_t size;
} fs_entry_t;

void fs_init(void);

fs_status_t fs_pwd(char *out, size_t cap);
fs_status_t fs_chdir(const char *path);
fs_status_t fs_mkdir(const char *path);
fs_status_t fs_mkdir_p(const char *path);
fs_status_t fs_touch(const char *path);
fs_status_t fs_write(const char *path, const char *data, bool append);
fs_status_t fs_read(const char *path, char *out, size_t cap, size_t *out_len);
fs_status_t fs_list(const char *path, fs_entry_t *entries, size_t max_entries, size_t *out_count);
fs_status_t fs_stat(const char *path, fs_entry_t *out);

const char *fs_status_string(fs_status_t status);

#endif
