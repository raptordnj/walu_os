#include <kernel/storage.h>
#include <kernel/string.h>

#define STORAGE_MAX_DEVICES 8
#define STORAGE_MAX_MOUNTS 8

#define STORAGE_NAME_MAX 16
#define STORAGE_PATH_MAX 32
#define STORAGE_FSTYPE_MAX 16
#define STORAGE_LABEL_MAX 32
#define STORAGE_UUID_MAX 37
#define STORAGE_TARGET_MAX 64

typedef struct {
    bool in_use;
    char name[STORAGE_NAME_MAX];
    char path[STORAGE_PATH_MAX];
    uint64_t size_bytes;
    bool removable;
    bool read_only;
    bool formatted;
    char fstype[STORAGE_FSTYPE_MAX];
    char label[STORAGE_LABEL_MAX];
    char uuid[STORAGE_UUID_MAX];
    int mount_slot;
} storage_device_t;

typedef struct {
    bool in_use;
    int device_slot;
    char target[STORAGE_TARGET_MAX];
    bool read_write;
    bool trusted;
} storage_mount_t;

static storage_device_t g_devices[STORAGE_MAX_DEVICES];
static storage_mount_t g_mounts[STORAGE_MAX_MOUNTS];
static uint32_t g_uuid_generation = 1;

static size_t storage_strnlen_local(const char *s, size_t max_len) {
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (n < max_len && s[n] != '\0') {
        n++;
    }
    return n;
}

static void storage_copy(char *dst, size_t dst_len, const char *src) {
    size_t n;
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    n = storage_strnlen_local(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool storage_is_absolute_path(const char *path) {
    return path && path[0] == '/';
}

static bool storage_is_valid_device_path(const char *path) {
    return path && strncmp(path, "/dev/", 5) == 0 && storage_strnlen_local(path, STORAGE_PATH_MAX) > 5;
}

static bool storage_is_supported_fstype(const char *fstype) {
    return fstype &&
           (strcmp(fstype, "ext4") == 0 || strcmp(fstype, "vfat") == 0 || strcmp(fstype, "xfs") == 0);
}

static char hex_digit(uint8_t value) {
    if (value < 10) {
        return (char)('0' + value);
    }
    return (char)('a' + (value - 10));
}

static void storage_write_hex_u32(char *dst, uint32_t value, size_t digits) {
    for (size_t i = 0; i < digits; i++) {
        size_t shift = (digits - 1 - i) * 4;
        dst[i] = hex_digit((uint8_t)((value >> shift) & 0xFu));
    }
}

static void storage_make_uuid(char out[STORAGE_UUID_MAX], uint32_t dev_slot) {
    uint32_t a = 0xA11C0000u | ((g_uuid_generation + dev_slot) & 0xFFFFu);
    uint32_t b = 0xBEEFu + g_uuid_generation + dev_slot;
    uint32_t c = 0x1000u | ((g_uuid_generation + dev_slot) & 0x0FFFu);
    uint32_t d = 0x8000u | ((dev_slot + 1u) & 0x0FFFu);
    uint32_t e_hi = 0xC0DEu;
    uint32_t e_lo = (g_uuid_generation * 37u) + dev_slot;

    storage_write_hex_u32(out + 0, a, 8);
    out[8] = '-';
    storage_write_hex_u32(out + 9, b, 4);
    out[13] = '-';
    storage_write_hex_u32(out + 14, c, 4);
    out[18] = '-';
    storage_write_hex_u32(out + 19, d, 4);
    out[23] = '-';
    storage_write_hex_u32(out + 24, e_hi, 4);
    storage_write_hex_u32(out + 28, e_lo, 8);
    out[36] = '\0';
}

static int storage_find_device_slot(const char *path) {
    for (int i = 0; i < STORAGE_MAX_DEVICES; i++) {
        if (g_devices[i].in_use && strcmp(g_devices[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int storage_find_mount_slot_by_target(const char *target) {
    for (int i = 0; i < STORAGE_MAX_MOUNTS; i++) {
        if (g_mounts[i].in_use && strcmp(g_mounts[i].target, target) == 0) {
            return i;
        }
    }
    return -1;
}

static int storage_find_free_mount_slot(void) {
    for (int i = 0; i < STORAGE_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) {
            return i;
        }
    }
    return -1;
}

static int storage_add_device(const char *name, const char *path, uint64_t size_bytes,
                              bool removable, bool read_only, bool formatted,
                              const char *fstype, const char *label) {
    for (int i = 0; i < STORAGE_MAX_DEVICES; i++) {
        if (!g_devices[i].in_use) {
            g_devices[i].in_use = true;
            storage_copy(g_devices[i].name, sizeof(g_devices[i].name), name);
            storage_copy(g_devices[i].path, sizeof(g_devices[i].path), path);
            g_devices[i].size_bytes = size_bytes;
            g_devices[i].removable = removable;
            g_devices[i].read_only = read_only;
            g_devices[i].formatted = formatted;
            storage_copy(g_devices[i].fstype, sizeof(g_devices[i].fstype), formatted ? fstype : "");
            storage_copy(g_devices[i].label, sizeof(g_devices[i].label), formatted ? label : "");
            g_devices[i].mount_slot = -1;
            storage_make_uuid(g_devices[i].uuid, (uint32_t)i);
            g_uuid_generation++;
            return i;
        }
    }
    return -1;
}

static void storage_fill_device_info(size_t index, storage_device_info_t *out) {
    const storage_device_t *d = &g_devices[index];
    out->name = d->name;
    out->path = d->path;
    out->size_bytes = d->size_bytes;
    out->removable = d->removable;
    out->read_only = d->read_only;
    out->formatted = d->formatted;
    out->fstype = d->formatted ? d->fstype : "";
    out->label = d->formatted ? d->label : "";
    out->uuid = d->formatted ? d->uuid : "";
    if (d->mount_slot >= 0 && d->mount_slot < STORAGE_MAX_MOUNTS && g_mounts[d->mount_slot].in_use) {
        out->mountpoint = g_mounts[d->mount_slot].target;
        out->mount_read_write = g_mounts[d->mount_slot].read_write;
    } else {
        out->mountpoint = "";
        out->mount_read_write = false;
    }
}

void storage_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_mounts, 0, sizeof(g_mounts));
    g_uuid_generation = 1;

    (void)storage_add_device("ram0", "/dev/ram0", 64ULL * 1024ULL * 1024ULL, false, false, true, "ext4", "rootfs");
    (void)storage_add_device("usb0", "/dev/usb0", 32ULL * 1024ULL * 1024ULL, true, false, false, "", "");

    if (g_devices[0].in_use) {
        g_mounts[0].in_use = true;
        g_mounts[0].device_slot = 0;
        storage_copy(g_mounts[0].target, sizeof(g_mounts[0].target), "/");
        g_mounts[0].read_write = true;
        g_mounts[0].trusted = true;
        g_devices[0].mount_slot = 0;
    }
}

size_t storage_device_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < STORAGE_MAX_DEVICES; i++) {
        if (g_devices[i].in_use) {
            count++;
        }
    }
    return count;
}

bool storage_device_info(size_t index, storage_device_info_t *out) {
    size_t seen = 0;
    if (!out) {
        return false;
    }
    for (size_t i = 0; i < STORAGE_MAX_DEVICES; i++) {
        if (!g_devices[i].in_use) {
            continue;
        }
        if (seen == index) {
            storage_fill_device_info(i, out);
            return true;
        }
        seen++;
    }
    return false;
}

bool storage_find_device(const char *path, storage_device_info_t *out) {
    int slot;
    if (!path || !out) {
        return false;
    }
    slot = storage_find_device_slot(path);
    if (slot < 0) {
        return false;
    }
    storage_fill_device_info((size_t)slot, out);
    return true;
}

storage_status_t storage_mount(const char *device, const char *target,
                               bool read_write, bool trusted, bool force, bool dry_run) {
    int device_slot;
    int mount_slot;
    storage_device_t *d;

    if (!storage_is_valid_device_path(device) || !storage_is_absolute_path(target)) {
        return STORAGE_ERR_INVALID;
    }

    device_slot = storage_find_device_slot(device);
    if (device_slot < 0) {
        return STORAGE_ERR_NOT_FOUND;
    }

    d = &g_devices[device_slot];
    if (!d->formatted) {
        return STORAGE_ERR_NO_FILESYSTEM;
    }
    if (d->mount_slot >= 0) {
        return STORAGE_ERR_ALREADY_MOUNTED;
    }
    if (storage_find_mount_slot_by_target(target) >= 0) {
        return STORAGE_ERR_BUSY;
    }
    if (d->read_only && read_write) {
        return STORAGE_ERR_POLICY;
    }
    if (d->removable && !trusted && read_write && !force) {
        return STORAGE_ERR_POLICY;
    }

    mount_slot = storage_find_free_mount_slot();
    if (mount_slot < 0) {
        return STORAGE_ERR_BUSY;
    }

    if (dry_run) {
        return STORAGE_OK;
    }

    g_mounts[mount_slot].in_use = true;
    g_mounts[mount_slot].device_slot = device_slot;
    storage_copy(g_mounts[mount_slot].target, sizeof(g_mounts[mount_slot].target), target);
    g_mounts[mount_slot].read_write = read_write && (!d->removable || trusted || force);
    g_mounts[mount_slot].trusted = trusted;
    d->mount_slot = mount_slot;
    return STORAGE_OK;
}

storage_status_t storage_umount_target(const char *target, bool dry_run) {
    int slot;
    if (!target || *target == '\0') {
        return STORAGE_ERR_INVALID;
    }

    slot = storage_find_mount_slot_by_target(target);
    if (slot < 0 && storage_is_valid_device_path(target)) {
        int device_slot = storage_find_device_slot(target);
        if (device_slot >= 0) {
            slot = g_devices[device_slot].mount_slot;
        }
    }

    if (slot < 0 || slot >= STORAGE_MAX_MOUNTS || !g_mounts[slot].in_use) {
        return STORAGE_ERR_NOT_MOUNTED;
    }

    if (dry_run) {
        return STORAGE_OK;
    }

    if (g_mounts[slot].device_slot >= 0 && g_mounts[slot].device_slot < STORAGE_MAX_DEVICES) {
        g_devices[g_mounts[slot].device_slot].mount_slot = -1;
    }
    memset(&g_mounts[slot], 0, sizeof(g_mounts[slot]));
    return STORAGE_OK;
}

storage_status_t storage_fsck(const char *device, bool force, bool dry_run, bool confirmed) {
    int slot;
    storage_device_t *d;

    if (!storage_is_valid_device_path(device)) {
        return STORAGE_ERR_INVALID;
    }
    slot = storage_find_device_slot(device);
    if (slot < 0) {
        return STORAGE_ERR_NOT_FOUND;
    }
    d = &g_devices[slot];
    if (!d->formatted) {
        return STORAGE_ERR_NO_FILESYSTEM;
    }
    if (d->mount_slot >= 0) {
        return STORAGE_ERR_BUSY;
    }
    if (force && !confirmed) {
        return STORAGE_ERR_CONFIRMATION_REQUIRED;
    }
    if (dry_run) {
        return STORAGE_OK;
    }
    return STORAGE_OK;
}

storage_status_t storage_format(const char *device, const char *fstype, const char *label,
                                bool force, bool dry_run, bool confirmed) {
    int slot;
    storage_device_t *d;
    const char *use_fstype = fstype && fstype[0] ? fstype : "ext4";

    if (!storage_is_valid_device_path(device)) {
        return STORAGE_ERR_INVALID;
    }
    if (!storage_is_supported_fstype(use_fstype)) {
        return STORAGE_ERR_INVALID;
    }
    slot = storage_find_device_slot(device);
    if (slot < 0) {
        return STORAGE_ERR_NOT_FOUND;
    }

    d = &g_devices[slot];
    if (d->mount_slot >= 0) {
        return STORAGE_ERR_BUSY;
    }
    if (!force || !confirmed) {
        return STORAGE_ERR_CONFIRMATION_REQUIRED;
    }
    if (dry_run) {
        return STORAGE_OK;
    }

    d->formatted = true;
    storage_copy(d->fstype, sizeof(d->fstype), use_fstype);
    storage_copy(d->label, sizeof(d->label), label ? label : "");
    storage_make_uuid(d->uuid, (uint32_t)slot);
    g_uuid_generation++;
    return STORAGE_OK;
}

const char *storage_status_string(storage_status_t status) {
    switch (status) {
        case STORAGE_OK: return "ok";
        case STORAGE_ERR_NOT_FOUND: return "not-found";
        case STORAGE_ERR_INVALID: return "invalid-args";
        case STORAGE_ERR_ALREADY_MOUNTED: return "already-mounted";
        case STORAGE_ERR_NOT_MOUNTED: return "not-mounted";
        case STORAGE_ERR_BUSY: return "busy";
        case STORAGE_ERR_POLICY: return "policy-denied";
        case STORAGE_ERR_CONFIRMATION_REQUIRED: return "confirmation-required";
        case STORAGE_ERR_NO_FILESYSTEM: return "no-filesystem";
        default: return "unknown";
    }
}
