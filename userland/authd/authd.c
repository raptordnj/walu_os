#include "../common/log.h"

#include <ctype.h>
#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int min_len;
    int require_upper;
    int require_lower;
    int require_digit;
    int require_symbol;
} password_policy_t;

static int check_policy(const char *password, const password_policy_t *policy) {
    int has_upper = 0;
    int has_lower = 0;
    int has_digit = 0;
    int has_symbol = 0;
    size_t len = strlen(password);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)password[i];
        if (isupper(c)) {
            has_upper = 1;
        } else if (islower(c)) {
            has_lower = 1;
        } else if (isdigit(c)) {
            has_digit = 1;
        } else {
            has_symbol = 1;
        }
    }

    if ((int)len < policy->min_len) {
        return 1;
    }
    if (policy->require_upper && !has_upper) {
        return 2;
    }
    if (policy->require_lower && !has_lower) {
        return 3;
    }
    if (policy->require_digit && !has_digit) {
        return 4;
    }
    if (policy->require_symbol && !has_symbol) {
        return 5;
    }
    return 0;
}

static int check_shadow_hash_state(const char *hash_field) {
    if (hash_field[0] == '\0') {
        return 1; /* no hash set */
    }
    if (hash_field[0] == '!' || hash_field[0] == '*') {
        return 2; /* account locked */
    }
    return 0;
}

static void usage(void) {
    puts("Usage:");
    puts("  authd policy-check <password>");
    puts("  authd policy-check --stdin");
    puts("  authd shadow-state <hash-field>");
    puts("  authd verify --user <name> [--shadow <path>] --password-stdin");
}

static int parse_shadow_hash(const char *shadow_path, const char *user, char *out_hash, size_t out_len) {
    FILE *fp = fopen(shadow_path, "r");
    char line[1024];
    int found = 0;

    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *save = NULL;
        char *name = strtok_r(line, ":\n", &save);
        char *hash = strtok_r(NULL, ":\n", &save);
        if (!name || !hash) {
            continue;
        }
        if (strcmp(name, user) == 0) {
            snprintf(out_hash, out_len, "%s", hash);
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found ? 0 : 1;
}

static int verify_user_password(const char *user, const char *password, const char *shadow_path) {
    char hash[512];
    char *computed;
    int state;
    int rc = parse_shadow_hash(shadow_path, user, hash, sizeof(hash));
    if (rc != 0) {
        return rc == 1 ? 7 : 8;
    }

    state = check_shadow_hash_state(hash);
    if (state != 0) {
        return state == 1 ? 9 : 10;
    }

    computed = crypt(password, hash);
    if (!computed) {
        return 11;
    }

    return strcmp(computed, hash) == 0 ? 0 : 12;
}

int main(int argc, char **argv) {
    password_policy_t policy = {
        .min_len = 12,
        .require_upper = 1,
        .require_lower = 1,
        .require_digit = 1,
        .require_symbol = 1,
    };

    if (argc < 3) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "policy-check") == 0) {
        char pw_buf[512];
        const char *password = NULL;
        int rc;

        if (strcmp(argv[2], "--stdin") == 0) {
            if (!fgets(pw_buf, sizeof(pw_buf), stdin)) {
                log_error("authd", "failed reading password from stdin");
                return 6;
            }
            pw_buf[strcspn(pw_buf, "\r\n")] = '\0';
            password = pw_buf;
        } else {
            password = argv[2];
        }

        rc = check_policy(password, &policy);
        if (rc != 0) {
            log_error("authd", "password policy check failed");
            return rc;
        }
        log_info("authd", "password policy check passed");
        return 0;
    }

    if (strcmp(argv[1], "verify") == 0) {
        const char *user = NULL;
        const char *shadow_path = "/etc/shadow";
        char pw_buf[512];
        int got_pw_stdin = 0;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
                user = argv[++i];
            } else if (strcmp(argv[i], "--shadow") == 0 && i + 1 < argc) {
                shadow_path = argv[++i];
            } else if (strcmp(argv[i], "--password-stdin") == 0) {
                got_pw_stdin = 1;
            } else {
                usage();
                return 1;
            }
        }

        if (!user || !got_pw_stdin) {
            usage();
            return 1;
        }

        if (!fgets(pw_buf, sizeof(pw_buf), stdin)) {
            log_error("authd", "failed to read password from stdin");
            return 6;
        }
        pw_buf[strcspn(pw_buf, "\r\n")] = '\0';

        {
            int rc = verify_user_password(user, pw_buf, shadow_path);
            memset(pw_buf, 0, sizeof(pw_buf));
            if (rc != 0) {
                log_error("authd", "password verify failed");
                return rc;
            }
        }

        log_info("authd", "password verify succeeded");
        return 0;
    }

    if (strcmp(argv[1], "shadow-state") == 0) {
        int rc = check_shadow_hash_state(argv[2]);
        if (rc == 0) {
            puts("state=active");
            return 0;
        }
        if (rc == 1) {
            puts("state=unset");
            return 2;
        }
        puts("state=locked");
        return 3;
    }

    usage();
    return 1;
}
