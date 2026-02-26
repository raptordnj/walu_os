#include "log.h"

#include <stdio.h>
#include <time.h>

static void log_emit(const char *level, const char *component, const char *message) {
    time_t now = time(NULL);
    struct tm tm_now;
    char ts[32];

    if (gmtime_r(&now, &tm_now) == NULL) {
        return;
    }

    if (strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_now) == 0) {
        return;
    }

    fprintf(stderr, "ts=%s level=%s component=%s msg=\"%s\"\n", ts, level, component, message);
}

void log_info(const char *component, const char *message) {
    log_emit("info", component, message);
}

void log_error(const char *component, const char *message) {
    log_emit("error", component, message);
}
