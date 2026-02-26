#include "../common/log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CMD_BUF_SIZE 1024
#define FIELD_BUF_SIZE 256

typedef struct {
    const char *device;
    const char *target;
    const char *fstype;
    const char *label;
    const char *options;
    const char *confirm;
    bool dry_run;
    bool force;
    bool yes;
    bool trusted;
    bool read_write;
    bool lazy;
    bool mkdir_parents;
    bool json;
    bool all;
} storaged_args_t;

static const char *audit_log_path(void) {
    return "/tmp/walu_storaged_audit.log";
}

static void audit_event(const char *op, const char *device, const char *target,
                        const char *result, const char *detail) {
    FILE *fp = fopen(audit_log_path(), "a");
    if (!fp) {
        return;
    }

    fprintf(fp, "op=%s device=%s target=%s result=%s detail=%s\n",
            op ? op : "-",
            device ? device : "-",
            target ? target : "-",
            result ? result : "-",
            detail ? detail : "-");
    fclose(fp);
}

static void usage(void) {
    puts("Usage:");
    puts("  storaged lsblk [--json] [--device <path>]");
    puts("  storaged blkid [--device <path>]");
    puts("  storaged probe --device <path>");
    puts("  storaged mount --device <path> --target <dir> [--fstype <type>] [--options <opts>]");
    puts("                 [--read-write] [--trusted] [--mkdir] [--dry-run] [--force]");
    puts("  storaged umount --target <dir|device> [--lazy] [--dry-run]");
    puts("  storaged fsck --device <path> [--dry-run] [--force --confirm <path> --yes]");
    puts("  storaged format --device <path> [--fstype ext4|vfat|xfs] [--label <name>]");
    puts("                  [--dry-run] [--force --confirm <path> --yes]");
    puts("Safety:");
    puts("  unknown removable media defaults to read-only mount options");
    puts("  destructive operations require --force, --confirm <exact-device>, and --yes");
}

static bool is_safe_token(const char *s) {
    size_t i;
    if (!s || *s == '\0') {
        return false;
    }
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-' || c == ':' || c == ',' || c == '=')) {
            return false;
        }
    }
    return true;
}

static bool is_valid_block_path(const char *path) {
    return path && strncmp(path, "/dev/", 5) == 0 && strlen(path) > 5 && is_safe_token(path);
}

static bool is_absolute_path(const char *path) {
    return path && path[0] == '/' && is_safe_token(path);
}

static bool is_block_device(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISBLK(st.st_mode);
}

static bool path_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static void trim_newline(char *s) {
    if (!s) {
        return;
    }
    s[strcspn(s, "\r\n")] = '\0';
}

static void print_command(char *const argv[]) {
    int i = 0;
    printf("cmd:");
    while (argv[i]) {
        printf(" %s", argv[i]);
        i++;
    }
    printf("\n");
}

static int run_command(char *const argv[], bool dry_run) {
    pid_t pid;
    int status = 0;

    if (!argv || !argv[0]) {
        return 1;
    }

    if (dry_run) {
        print_command(argv);
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        return 1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

static int run_command_capture(char *const argv[], char *out, size_t out_len) {
    int pipefd[2];
    pid_t pid;
    int status = 0;
    ssize_t nread;

    if (!argv || !argv[0] || !out || out_len == 0) {
        return 1;
    }

    out[0] = '\0';

    if (pipe(pipefd) != 0) {
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (devnull >= 0) {
            (void)dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    nread = read(pipefd[0], out, out_len - 1);
    if (nread < 0) {
        nread = 0;
    }
    out[nread] = '\0';
    close(pipefd[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }

    trim_newline(out);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
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

static bool is_target_mounted(const char *target) {
    FILE *fp = fopen("/proc/self/mounts", "r");
    char dev[256];
    char mountpt[256];
    char fstype[64];
    char opts[256];

    if (!fp) {
        return false;
    }

    while (fscanf(fp, "%255s %255s %63s %255s %*d %*d", dev, mountpt, fstype, opts) == 4) {
        if (strcmp(mountpt, target) == 0) {
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

static bool ensure_dir(const char *path) {
    char tmp[CMD_BUF_SIZE];
    char *p;

    if (!path || !is_absolute_path(path) || strlen(path) >= sizeof(tmp)) {
        return false;
    }

    snprintf(tmp, sizeof(tmp), "%s", path);

    if (tmp[1] == '\0') {
        return true;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] != '\0' && !path_is_dir(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }

    if (!path_is_dir(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

static int parse_common_flags(int argc, char **argv, storaged_args_t *args, int start) {
    int i;
    memset(args, 0, sizeof(*args));

    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            args->device = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            args->target = argv[++i];
        } else if (strcmp(argv[i], "--fstype") == 0 && i + 1 < argc) {
            args->fstype = argv[++i];
        } else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) {
            args->label = argv[++i];
        } else if (strcmp(argv[i], "--options") == 0 && i + 1 < argc) {
            args->options = argv[++i];
        } else if (strcmp(argv[i], "--confirm") == 0 && i + 1 < argc) {
            args->confirm = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            args->dry_run = true;
        } else if (strcmp(argv[i], "--force") == 0) {
            args->force = true;
        } else if (strcmp(argv[i], "--yes") == 0) {
            args->yes = true;
        } else if (strcmp(argv[i], "--trusted") == 0) {
            args->trusted = true;
        } else if (strcmp(argv[i], "--read-write") == 0) {
            args->read_write = true;
        } else if (strcmp(argv[i], "--lazy") == 0) {
            args->lazy = true;
        } else if (strcmp(argv[i], "--mkdir") == 0) {
            args->mkdir_parents = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            args->json = true;
        } else if (strcmp(argv[i], "--all") == 0) {
            args->all = true;
        } else {
            return 1;
        }
    }

    return 0;
}

static int require_destructive_confirmation(const storaged_args_t *args, const char *op) {
    if (!args->force || !args->yes || !args->confirm || !args->device || strcmp(args->confirm, args->device) != 0) {
        log_error("storaged", "missing mandatory destructive-operation confirmation flags");
        usage();
        audit_event(op, args->device, args->target, "deny", "missing-force-confirm-yes");
        return 3;
    }
    return 0;
}

static bool lsblk_field(const char *device, const char *field, char *out, size_t out_len) {
    char *argv_cmd[] = {"lsblk", "-ndo", (char *)field, (char *)device, NULL};
    return run_command_capture(argv_cmd, out, out_len) == 0 && out[0] != '\0';
}

static bool blkid_field(const char *device, const char *field, char *out, size_t out_len) {
    char *argv_cmd[] = {"blkid", "-o", "value", "-s", (char *)field, (char *)device, NULL};
    return run_command_capture(argv_cmd, out, out_len) == 0 && out[0] != '\0';
}

static int command_lsblk(int argc, char **argv) {
    storaged_args_t args;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0) {
        usage();
        return 1;
    }

    if (args.device && !is_valid_block_path(args.device)) {
        log_error("storaged", "invalid --device path");
        audit_event("lsblk", args.device, NULL, "deny", "invalid-path");
        return 2;
    }

    if (args.json) {
        if (args.device) {
            char *cmd[] = {"lsblk", "-J", "-o", "NAME,PATH,SIZE,TYPE,FSTYPE,RM,RO,MOUNTPOINTS", (char *)args.device, NULL};
            rc = run_command(cmd, args.dry_run);
        } else {
            char *cmd[] = {"lsblk", "-J", "-o", "NAME,PATH,SIZE,TYPE,FSTYPE,RM,RO,MOUNTPOINTS", NULL};
            rc = run_command(cmd, args.dry_run);
        }
    } else {
        if (args.device) {
            char *cmd[] = {"lsblk", "-o", "NAME,PATH,MAJ:MIN,SIZE,TYPE,FSTYPE,RM,RO,MOUNTPOINTS", (char *)args.device, NULL};
            rc = run_command(cmd, args.dry_run);
        } else {
            char *cmd[] = {"lsblk", "-o", "NAME,PATH,MAJ:MIN,SIZE,TYPE,FSTYPE,RM,RO,MOUNTPOINTS", NULL};
            rc = run_command(cmd, args.dry_run);
        }
    }

    audit_event("lsblk", args.device, NULL, rc == 0 ? (args.dry_run ? "dry-run" : "ok") : "error", "lsblk");
    return rc;
}

static int command_blkid(int argc, char **argv) {
    storaged_args_t args;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0) {
        usage();
        return 1;
    }

    if (args.device && !is_valid_block_path(args.device)) {
        log_error("storaged", "invalid --device path");
        audit_event("blkid", args.device, NULL, "deny", "invalid-path");
        return 2;
    }

    if (args.device) {
        char *cmd[] = {"blkid", (char *)args.device, NULL};
        rc = run_command(cmd, args.dry_run);
    } else {
        char *cmd[] = {"blkid", NULL};
        rc = run_command(cmd, args.dry_run);
    }

    audit_event("blkid", args.device, NULL, rc == 0 ? (args.dry_run ? "dry-run" : "ok") : "error", "blkid");
    return rc;
}

static int command_probe(int argc, char **argv) {
    storaged_args_t args;
    char field[FIELD_BUF_SIZE];
    bool removable = false;

    if (parse_common_flags(argc, argv, &args, 2) != 0 || !args.device) {
        usage();
        return 1;
    }

    if (!is_valid_block_path(args.device)) {
        log_error("storaged", "invalid device path for probe");
        audit_event("probe", args.device, NULL, "deny", "invalid-path");
        return 2;
    }

    printf("device=%s\n", args.device);
    printf("block_device=%s\n", is_block_device(args.device) ? "yes" : "no");
    printf("mounted=%s\n", is_device_mounted(args.device) ? "yes" : "no");

    if (lsblk_field(args.device, "SIZE", field, sizeof(field))) {
        printf("size=%s\n", field);
    } else {
        printf("size=unknown\n");
    }

    if (lsblk_field(args.device, "RM", field, sizeof(field))) {
        removable = strcmp(field, "1") == 0;
        printf("removable=%s\n", removable ? "yes" : "no");
    } else {
        printf("removable=unknown\n");
    }

    if (blkid_field(args.device, "TYPE", field, sizeof(field))) {
        printf("fstype=%s\n", field);
    } else {
        printf("fstype=unknown\n");
    }

    if (blkid_field(args.device, "UUID", field, sizeof(field))) {
        printf("uuid=%s\n", field);
    } else {
        printf("uuid=unknown\n");
    }

    if (blkid_field(args.device, "LABEL", field, sizeof(field))) {
        printf("label=%s\n", field);
    } else {
        printf("label=unknown\n");
    }

    audit_event("probe", args.device, NULL, "ok", "reported");
    return 0;
}

static bool build_mount_options(const storaged_args_t *args, bool removable,
                                char *out, size_t out_len) {
    const char *base;
    size_t n;

    if (!out || out_len == 0) {
        return false;
    }

    if (args->options && !is_safe_token(args->options)) {
        return false;
    }

    if (removable && !args->trusted) {
        base = args->read_write ? "rw,nosuid,nodev,noexec,relatime" : "ro,nosuid,nodev,noexec,relatime";
    } else {
        base = args->read_write ? "rw,relatime" : "ro,relatime";
    }

    n = snprintf(out, out_len, "%s", base);
    if (n >= out_len) {
        return false;
    }

    if (args->options && args->options[0] != '\0') {
        n = snprintf(out, out_len, "%s,%s", base, args->options);
        if (n >= out_len) {
            return false;
        }
    }

    return true;
}

static int command_mount(int argc, char **argv) {
    storaged_args_t args;
    char field[FIELD_BUF_SIZE];
    char opts[CMD_BUF_SIZE];
    bool removable = false;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0 || !args.device || !args.target) {
        usage();
        return 1;
    }

    if (!is_valid_block_path(args.device) || !is_absolute_path(args.target)) {
        log_error("storaged", "invalid --device or --target path");
        audit_event("mount", args.device, args.target, "deny", "invalid-path");
        return 2;
    }

    if (!is_block_device(args.device)) {
        log_error("storaged", "device path is not a block device");
        audit_event("mount", args.device, args.target, "deny", "not-block-device");
        return 2;
    }

    if (!path_is_dir(args.target)) {
        if (args.mkdir_parents) {
            if (!ensure_dir(args.target)) {
                log_error("storaged", "failed to create target directory");
                audit_event("mount", args.device, args.target, "deny", "mkdir-failed");
                return 2;
            }
        } else {
            log_error("storaged", "target mount directory does not exist (use --mkdir)");
            audit_event("mount", args.device, args.target, "deny", "target-missing");
            return 2;
        }
    }

    if (is_target_mounted(args.target)) {
        log_error("storaged", "target already mounted");
        audit_event("mount", args.device, args.target, "deny", "target-mounted");
        return 4;
    }

    if (lsblk_field(args.device, "RM", field, sizeof(field))) {
        removable = strcmp(field, "1") == 0;
    }

    if (removable && !args.trusted && args.read_write && !args.force) {
        log_error("storaged", "refusing read-write mount for untrusted removable device without --force");
        audit_event("mount", args.device, args.target, "deny", "rw-untrusted-removable");
        return 3;
    }

    if (!build_mount_options(&args, removable, opts, sizeof(opts))) {
        log_error("storaged", "invalid mount options");
        audit_event("mount", args.device, args.target, "deny", "invalid-options");
        return 2;
    }

    if (args.fstype && !is_safe_token(args.fstype)) {
        log_error("storaged", "invalid fstype");
        audit_event("mount", args.device, args.target, "deny", "invalid-fstype");
        return 2;
    }

    if (args.fstype) {
        char *cmd[] = {"mount", "-t", (char *)args.fstype, "-o", opts, (char *)args.device, (char *)args.target, NULL};
        rc = run_command(cmd, args.dry_run);
    } else {
        char *cmd[] = {"mount", "-o", opts, (char *)args.device, (char *)args.target, NULL};
        rc = run_command(cmd, args.dry_run);
    }

    if (rc != 0) {
        log_error("storaged", "mount command failed");
    } else {
        log_info("storaged", "mount operation succeeded");
    }

    audit_event("mount", args.device, args.target, rc == 0 ? (args.dry_run ? "dry-run" : "ok") : "error",
                removable && !args.trusted ? "policy-untrusted-removable" : "policy-default");
    return rc;
}

static int command_umount(int argc, char **argv) {
    storaged_args_t args;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0 || !args.target) {
        usage();
        return 1;
    }

    if (!is_absolute_path(args.target) && !is_valid_block_path(args.target)) {
        log_error("storaged", "invalid umount target");
        audit_event("umount", NULL, args.target, "deny", "invalid-target");
        return 2;
    }

    if (args.lazy) {
        char *cmd[] = {"umount", "-l", (char *)args.target, NULL};
        rc = run_command(cmd, args.dry_run);
    } else {
        char *cmd[] = {"umount", (char *)args.target, NULL};
        rc = run_command(cmd, args.dry_run);
    }

    if (rc != 0) {
        log_error("storaged", "umount command failed");
    } else {
        log_info("storaged", "umount operation succeeded");
    }

    audit_event("umount", NULL, args.target, rc == 0 ? (args.dry_run ? "dry-run" : "ok") : "error",
                args.lazy ? "lazy" : "normal");
    return rc;
}

static int command_fsck(int argc, char **argv) {
    storaged_args_t args;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0 || !args.device) {
        usage();
        return 1;
    }

    if (!is_valid_block_path(args.device) || !is_block_device(args.device)) {
        log_error("storaged", "invalid fsck device");
        audit_event("fsck", args.device, NULL, "deny", "invalid-device");
        return 2;
    }

    if (args.force && !args.dry_run) {
        rc = require_destructive_confirmation(&args, "fsck");
        if (rc != 0) {
            return rc;
        }
        {
            char *cmd[] = {"fsck", "-y", (char *)args.device, NULL};
            rc = run_command(cmd, args.dry_run);
        }
    } else {
        char *cmd[] = {"fsck", "-n", (char *)args.device, NULL};
        rc = run_command(cmd, args.dry_run);
    }

    audit_event("fsck", args.device, NULL, rc == 0 ? (args.dry_run ? "dry-run" : "ok") : "error",
                args.force ? "force" : "readonly-check");
    return rc;
}

static bool is_supported_fstype(const char *fstype) {
    return strcmp(fstype, "ext4") == 0 || strcmp(fstype, "vfat") == 0 || strcmp(fstype, "xfs") == 0;
}

static int command_format(int argc, char **argv) {
    storaged_args_t args;
    const char *fstype;
    int rc;

    if (parse_common_flags(argc, argv, &args, 2) != 0 || !args.device) {
        usage();
        return 1;
    }

    fstype = args.fstype ? args.fstype : "ext4";

    if (!is_valid_block_path(args.device)) {
        log_error("storaged", "device path must be under /dev/");
        audit_event("format", args.device, NULL, "deny", "invalid-path");
        return 2;
    }

    if (!is_block_device(args.device)) {
        log_error("storaged", "device path is not a block device");
        audit_event("format", args.device, NULL, "deny", "not-block-device");
        return 2;
    }

    if (!is_supported_fstype(fstype) || !is_safe_token(fstype)) {
        log_error("storaged", "unsupported fstype (supported: ext4, vfat, xfs)");
        audit_event("format", args.device, NULL, "deny", "unsupported-fstype");
        return 2;
    }

    if (args.label && !is_safe_token(args.label)) {
        log_error("storaged", "invalid label");
        audit_event("format", args.device, NULL, "deny", "invalid-label");
        return 2;
    }

    if (args.dry_run) {
        if (strcmp(fstype, "ext4") == 0) {
            if (args.label) {
                char *cmd[] = {"mkfs.ext4", "-F", "-L", (char *)args.label, (char *)args.device, NULL};
                run_command(cmd, true);
            } else {
                char *cmd[] = {"mkfs.ext4", "-F", (char *)args.device, NULL};
                run_command(cmd, true);
            }
        } else if (strcmp(fstype, "vfat") == 0) {
            if (args.label) {
                char *cmd[] = {"mkfs.vfat", "-n", (char *)args.label, (char *)args.device, NULL};
                run_command(cmd, true);
            } else {
                char *cmd[] = {"mkfs.vfat", (char *)args.device, NULL};
                run_command(cmd, true);
            }
        } else {
            if (args.label) {
                char *cmd[] = {"mkfs.xfs", "-f", "-L", (char *)args.label, (char *)args.device, NULL};
                run_command(cmd, true);
            } else {
                char *cmd[] = {"mkfs.xfs", "-f", (char *)args.device, NULL};
                run_command(cmd, true);
            }
        }
        printf("dry-run: mounted=%s\n", is_device_mounted(args.device) ? "yes" : "no");
        audit_event("format", args.device, NULL, "dry-run", "policy-check-only");
        return 0;
    }

    if (is_device_mounted(args.device)) {
        log_error("storaged", "refusing to format a mounted device");
        audit_event("format", args.device, NULL, "deny", "mounted");
        return 4;
    }

    rc = require_destructive_confirmation(&args, "format");
    if (rc != 0) {
        return rc;
    }

    if (strcmp(fstype, "ext4") == 0) {
        if (args.label) {
            char *cmd[] = {"mkfs.ext4", "-F", "-L", (char *)args.label, (char *)args.device, NULL};
            rc = run_command(cmd, false);
        } else {
            char *cmd[] = {"mkfs.ext4", "-F", (char *)args.device, NULL};
            rc = run_command(cmd, false);
        }
    } else if (strcmp(fstype, "vfat") == 0) {
        if (args.label) {
            char *cmd[] = {"mkfs.vfat", "-n", (char *)args.label, (char *)args.device, NULL};
            rc = run_command(cmd, false);
        } else {
            char *cmd[] = {"mkfs.vfat", (char *)args.device, NULL};
            rc = run_command(cmd, false);
        }
    } else {
        if (args.label) {
            char *cmd[] = {"mkfs.xfs", "-f", "-L", (char *)args.label, (char *)args.device, NULL};
            rc = run_command(cmd, false);
        } else {
            char *cmd[] = {"mkfs.xfs", "-f", (char *)args.device, NULL};
            rc = run_command(cmd, false);
        }
    }

    if (rc == 0) {
        log_info("storaged", "format operation succeeded");
    } else {
        log_error("storaged", "format command failed");
    }
    audit_event("format", args.device, NULL, rc == 0 ? "ok" : "error", fstype);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "lsblk") == 0) {
        return command_lsblk(argc, argv);
    }
    if (strcmp(argv[1], "blkid") == 0) {
        return command_blkid(argc, argv);
    }
    if (strcmp(argv[1], "probe") == 0) {
        return command_probe(argc, argv);
    }
    if (strcmp(argv[1], "mount") == 0) {
        return command_mount(argc, argv);
    }
    if (strcmp(argv[1], "umount") == 0) {
        return command_umount(argc, argv);
    }
    if (strcmp(argv[1], "fsck") == 0) {
        return command_fsck(argc, argv);
    }
    if (strcmp(argv[1], "format") == 0) {
        return command_format(argc, argv);
    }

    usage();
    return 1;
}
