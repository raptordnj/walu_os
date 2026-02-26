#ifndef WALU_STORAGE_H
#define WALU_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_NOT_FOUND,
    STORAGE_ERR_INVALID,
    STORAGE_ERR_ALREADY_MOUNTED,
    STORAGE_ERR_NOT_MOUNTED,
    STORAGE_ERR_BUSY,
    STORAGE_ERR_POLICY,
    STORAGE_ERR_CONFIRMATION_REQUIRED,
    STORAGE_ERR_NO_FILESYSTEM,
    STORAGE_ERR_FS,
} storage_status_t;

typedef struct {
    const char *name;
    const char *path;
    uint64_t size_bytes;
    bool removable;
    bool read_only;
    bool formatted;
    const char *fstype;
    const char *label;
    const char *uuid;
    const char *mountpoint;
    bool mount_read_write;
} storage_device_info_t;

void storage_init(void);
size_t storage_device_count(void);
bool storage_device_info(size_t index, storage_device_info_t *out);
bool storage_find_device(const char *path, storage_device_info_t *out);

storage_status_t storage_mount(const char *device, const char *target,
                               bool read_write, bool trusted, bool force, bool dry_run);
storage_status_t storage_umount_target(const char *target, bool dry_run);
storage_status_t storage_fsck(const char *device, bool force, bool dry_run, bool confirmed);
storage_status_t storage_format(const char *device, const char *fstype, const char *label,
                                bool force, bool dry_run, bool confirmed);
storage_status_t storage_install(const char *device, const char *target,
                                 bool force, bool dry_run, bool confirmed);

const char *storage_status_string(storage_status_t status);

#endif
