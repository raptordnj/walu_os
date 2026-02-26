#include "../common/log.h"

#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX_LEN 512

typedef struct {
    char description[128];
    char after[128];
    char requires[128];
    char exec_start[256];
    char user[64];
    char restart[32];
    char wanted_by[64];
} unit_service_t;

static char *trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end-- = '\0';
    }
    return s;
}

static bool parse_unit_file(const char *path, unit_service_t *unit) {
    FILE *fp = fopen(path, "r");
    char line[LINE_MAX_LEN];
    char section[32] = {0};
    int line_no = 0;

    if (!fp) {
        log_error("walud", "failed to open unit file");
        return false;
    }

    memset(unit, 0, sizeof(*unit));

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = trim(line);
        char *eq;
        line_no++;

        if (*p == '\0' || *p == '#' || *p == ';') {
            continue;
        }

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) {
                fclose(fp);
                fprintf(stderr, "walud: %s:%d: malformed section header\n", path, line_no);
                return false;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", p + 1);
            continue;
        }

        eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';

        {
            char *key = trim(p);
            char *value = trim(eq + 1);

            if (strcmp(section, "Unit") == 0) {
                if (strcmp(key, "Description") == 0) {
                    snprintf(unit->description, sizeof(unit->description), "%s", value);
                } else if (strcmp(key, "After") == 0) {
                    snprintf(unit->after, sizeof(unit->after), "%s", value);
                } else if (strcmp(key, "Requires") == 0) {
                    snprintf(unit->requires, sizeof(unit->requires), "%s", value);
                }
            } else if (strcmp(section, "Service") == 0) {
                if (strcmp(key, "ExecStart") == 0) {
                    snprintf(unit->exec_start, sizeof(unit->exec_start), "%s", value);
                } else if (strcmp(key, "User") == 0) {
                    snprintf(unit->user, sizeof(unit->user), "%s", value);
                } else if (strcmp(key, "Restart") == 0) {
                    snprintf(unit->restart, sizeof(unit->restart), "%s", value);
                }
            } else if (strcmp(section, "Install") == 0) {
                if (strcmp(key, "WantedBy") == 0) {
                    snprintf(unit->wanted_by, sizeof(unit->wanted_by), "%s", value);
                }
            }
        }
    }

    fclose(fp);
    return true;
}

static bool is_absolute_path(const char *path) {
    return path != NULL && path[0] == '/';
}

static bool validate_unit(const unit_service_t *unit) {
    if (unit->description[0] == '\0') {
        log_error("walud", "missing Unit.Description");
        return false;
    }
    if (unit->exec_start[0] == '\0') {
        log_error("walud", "missing Service.ExecStart");
        return false;
    }
    if (!is_absolute_path(unit->exec_start)) {
        log_error("walud", "Service.ExecStart must be an absolute path");
        return false;
    }
    if (unit->restart[0] != '\0' &&
        strcmp(unit->restart, "no") != 0 &&
        strcmp(unit->restart, "on-failure") != 0 &&
        strcmp(unit->restart, "always") != 0) {
        log_error("walud", "Service.Restart must be no|on-failure|always");
        return false;
    }
    return true;
}

static void print_unit(const unit_service_t *unit) {
    printf("Description=%s\n", unit->description);
    printf("After=%s\n", unit->after);
    printf("Requires=%s\n", unit->requires);
    printf("ExecStart=%s\n", unit->exec_start);
    printf("User=%s\n", unit->user);
    printf("Restart=%s\n", unit->restart);
    printf("WantedBy=%s\n", unit->wanted_by);
}

static void print_usage(void) {
    puts("Usage: walud <validate|show|start> <unit-file>");
}

static int split_exec_start(char *line, char **argv_out, int max_argv) {
    int argc = 0;
    char *save = NULL;
    char *tok = strtok_r(line, " \t", &save);
    while (tok && argc + 1 < max_argv) {
        argv_out[argc++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    argv_out[argc] = NULL;
    return argc;
}

static int apply_unit_user(const unit_service_t *unit) {
    struct passwd *pw;
    if (unit->user[0] == '\0' || strcmp(unit->user, "root") == 0) {
        return 0;
    }
    pw = getpwnam(unit->user);
    if (!pw) {
        log_error("walud", "Service.User not found");
        return -1;
    }
    if (setgid(pw->pw_gid) != 0) {
        log_error("walud", "setgid failed");
        return -1;
    }
    if (setuid(pw->pw_uid) != 0) {
        log_error("walud", "setuid failed");
        return -1;
    }
    return 0;
}

static int start_unit(const unit_service_t *unit) {
    pid_t pid;
    int status = 0;
    char exec_buf[sizeof(unit->exec_start)];
    char *argv_exec[32];
    int argc;

    snprintf(exec_buf, sizeof(exec_buf), "%s", unit->exec_start);
    argc = split_exec_start(exec_buf, argv_exec, (int)(sizeof(argv_exec) / sizeof(argv_exec[0])));
    if (argc <= 0) {
        log_error("walud", "ExecStart parse failed");
        return 5;
    }

    pid = fork();
    if (pid < 0) {
        log_error("walud", "fork failed");
        return 6;
    }

    if (pid == 0) {
        if (apply_unit_user(unit) != 0) {
            _exit(126);
        }
        execv(argv_exec[0], argv_exec);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        log_error("walud", "waitpid failed");
        return 7;
    }

    if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec == 0) {
            log_info("walud", "unit exited successfully");
        } else {
            log_error("walud", "unit exited with non-zero status");
        }
        return ec;
    }

    if (WIFSIGNALED(status)) {
        log_error("walud", "unit terminated by signal");
        return 128 + WTERMSIG(status);
    }

    return 8;
}

int main(int argc, char **argv) {
    unit_service_t unit;

    if (argc != 3) {
        print_usage();
        return 1;
    }

    if (!parse_unit_file(argv[2], &unit)) {
        return 2;
    }

    if (strcmp(argv[1], "validate") == 0) {
        if (!validate_unit(&unit)) {
            return 3;
        }
        log_info("walud", "unit validation succeeded");
        return 0;
    }

    if (strcmp(argv[1], "show") == 0) {
        print_unit(&unit);
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        if (!validate_unit(&unit)) {
            return 3;
        }
        return start_unit(&unit);
    }

    print_usage();
    return 1;
}
