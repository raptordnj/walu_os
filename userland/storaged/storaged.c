#include "../common/log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *device;
    bool dry_run;
    bool force;
    bool yes;
    const char *confirm;
} format_args_t;

static const char *audit_log_path(void) {
    return "/tmp/walu_storaged_audit.log";
}

static void audit_event(const char *op, const char *device, const char *result, const char *detail) {
    FILE *fp = fopen(audit_log_path(), "a");
    if (!fp) {
        return;
    }
    fprintf(fp, "op=%s device=%s result=%s detail=%s\n",
            op ? op : "-", device ? device : "-", result ? result : "-", detail ? detail : "-");
    fclose(fp);
}

static void usage(void) {
    puts("Usage:");
    puts("  storaged probe --device <path>");
    puts("  storaged format --device <path> [--dry-run] [--force --confirm <path> --yes]");
    puts("Safety:");
    puts("  destructive operations require --force, --confirm <exact-device>, and --yes");
}

static int parse_format_args(int argc, char **argv, format_args_t *args) {
    memset(args, 0, sizeof(*args));

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            args->device = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            args->dry_run = true;
        } else if (strcmp(argv[i], "--force") == 0) {
            args->force = true;
        } else if (strcmp(argv[i], "--yes") == 0) {
            args->yes = true;
        } else if (strcmp(argv[i], "--confirm") == 0 && i + 1 < argc) {
            args->confirm = argv[++i];
        } else {
            return 1;
        }
    }

    if (!args->device) {
        return 1;
    }

    return 0;
}

static bool is_valid_block_path(const char *path) {
    return strncmp(path, "/dev/", 5) == 0 && strlen(path) > 5;
}

static bool is_block_device(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISBLK(st.st_mode);
}

static bool is_device_mounted(const char *device) {
    FILE *fp = fopen("/proc/self/mounts", "r");
    char dev[256];
    char mountpt[256];
    char fstype[64];
    char opts[256];

    if (!fp) {
        return false;
    }

    while (fscanf(fp, "%255s %255s %63s %255s %*d %*d", dev, mountpt, fstype, opts) == 4) {
        if (strcmp(dev, device) == 0) {
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

static void print_probe_info(const char *device) {
    char cmd[512];
    char buf[256];
    FILE *fp;

    printf("device=%s\n", device);
    printf("block_device=%s\n", is_block_device(device) ? "yes" : "no");
    printf("mounted=%s\n", is_device_mounted(device) ? "yes" : "no");

    snprintf(cmd, sizeof(cmd), "lsblk -ndo SIZE %s 2>/dev/null", device);
    fp = popen(cmd, "r");
    if (fp && fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        printf("size=%s\n", buf);
    } else {
        printf("size=unknown\n");
    }
    if (fp) {
        pclose(fp);
    }

    snprintf(cmd, sizeof(cmd), "blkid -o value -s TYPE %s 2>/dev/null", device);
    fp = popen(cmd, "r");
    if (fp && fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        printf("fstype=%s\n", buf);
    } else {
        printf("fstype=unknown\n");
    }
    if (fp) {
        pclose(fp);
    }
}

static int command_probe(const char *device) {
    if (!device || !is_valid_block_path(device)) {
        log_error("storaged", "invalid device path for probe");
        audit_event("probe", device, "deny", "invalid-path");
        return 2;
    }
    print_probe_info(device);
    audit_event("probe", device, "ok", "reported");
    return 0;
}

static int command_format(const format_args_t *args) {
    if (!is_valid_block_path(args->device)) {
        log_error("storaged", "device path must be under /dev/");
        audit_event("format", args->device, "deny", "invalid-path");
        return 2;
    }

    if (!is_block_device(args->device)) {
        log_error("storaged", "device path is not a block device");
        audit_event("format", args->device, "deny", "not-block-device");
        return 2;
    }

    if (args->dry_run) {
        printf("dry-run: mkfs.ext4 %s\n", args->device);
        printf("dry-run: mounted=%s\n", is_device_mounted(args->device) ? "yes" : "no");
        audit_event("format", args->device, "dry-run", "policy-check-only");
        return 0;
    }

    if (is_device_mounted(args->device)) {
        log_error("storaged", "refusing to format a mounted device");
        audit_event("format", args->device, "deny", "mounted");
        return 4;
    }

    if (!args->force || !args->yes || !args->confirm || strcmp(args->confirm, args->device) != 0) {
        log_error("storaged", "missing mandatory destructive-operation confirmation flags");
        usage();
        audit_event("format", args->device, "deny", "missing-force-confirm-yes");
        return 3;
    }

    /*
     * Intentionally does not invoke mkfs directly in this scaffold.
     * Execution hook will be added with policy/audit integration.
     */
    printf("approved: mkfs.ext4 %s\n", args->device);
    log_info("storaged", "format request approved by force+confirm+yes policy");
    audit_event("format", args->device, "approved", "force-confirm-yes");
    return 0;
}

int main(int argc, char **argv) {
    format_args_t args;

    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "probe") == 0) {
        if (argc != 4 || strcmp(argv[2], "--device") != 0) {
            usage();
            return 1;
        }
        return command_probe(argv[3]);
    }

    if (strcmp(argv[1], "format") == 0) {
        if (parse_format_args(argc, argv, &args) != 0) {
            usage();
            return 1;
        }
        return command_format(&args);
    }

    usage();
    return 1;
}
