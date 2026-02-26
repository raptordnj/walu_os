#include <kernel/pty.h>
#include <kernel/session.h>
#include <kernel/string.h>

#define SESSION_MAX 16

typedef struct {
    bool in_use;
    int id;
    uint32_t leader_pid;
    int controlling_pty;
} session_entry_t;

static session_entry_t g_sessions[SESSION_MAX];
static int g_active_session_id = -1;
static uint64_t g_session_invalid_ops = 0;

static session_entry_t *session_find(int session_id) {
    for (int i = 0; i < SESSION_MAX; i++) {
        if (g_sessions[i].in_use && g_sessions[i].id == session_id) {
            return &g_sessions[i];
        }
    }
    return 0;
}

void session_init(void) {
    memset(g_sessions, 0, sizeof(g_sessions));
    g_active_session_id = -1;
    g_session_invalid_ops = 0;
}

int session_create(uint32_t leader_pid) {
    for (int i = 0; i < SESSION_MAX; i++) {
        if (!g_sessions[i].in_use) {
            g_sessions[i].in_use = true;
            g_sessions[i].id = i + 1;
            g_sessions[i].leader_pid = leader_pid;
            g_sessions[i].controlling_pty = -1;
            return g_sessions[i].id;
        }
    }
    g_session_invalid_ops++;
    return -1;
}

bool session_set_controlling_pty(int session_id, int pty_id) {
    session_entry_t *entry = session_find(session_id);
    if (!entry || !pty_is_valid(pty_id)) {
        g_session_invalid_ops++;
        return false;
    }
    entry->controlling_pty = pty_id;
    return true;
}

bool session_set_active(int session_id) {
    if (!session_find(session_id)) {
        g_session_invalid_ops++;
        return false;
    }
    g_active_session_id = session_id;
    return true;
}

int session_active_id(void) {
    return g_active_session_id;
}

int session_active_pty(void) {
    session_entry_t *entry = session_find(g_active_session_id);
    if (!entry) {
        return -1;
    }
    return entry->controlling_pty;
}

uint64_t session_invalid_ops(void) {
    return g_session_invalid_ops;
}
