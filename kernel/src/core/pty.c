#include <kernel/pty.h>
#include <kernel/string.h>

#define PTY_MAX 8
#define PTY_QUEUE_SIZE 2048

typedef struct {
    bool allocated;
    uint8_t m2s[PTY_QUEUE_SIZE];
    size_t m2s_head;
    size_t m2s_tail;
    uint8_t s2m[PTY_QUEUE_SIZE];
    size_t s2m_head;
    size_t s2m_tail;
} pty_slot_t;

static pty_slot_t g_ptys[PTY_MAX];
static uint64_t g_pty_dropped_bytes = 0;
static uint64_t g_pty_invalid_ops = 0;

static size_t pty_queue_write(uint8_t *queue, size_t *head, size_t *tail, const uint8_t *buf, size_t len) {
    size_t written = 0;

    while (written < len) {
        size_t next = (*head + 1) % PTY_QUEUE_SIZE;
        if (next == *tail) {
            g_pty_dropped_bytes += (len - written);
            break;
        }
        queue[*head] = buf[written];
        *head = next;
        written++;
    }

    return written;
}

static size_t pty_queue_read(uint8_t *queue, size_t *head, size_t *tail, uint8_t *buf, size_t len) {
    size_t read = 0;

    while (read < len && *tail != *head) {
        buf[read++] = queue[*tail];
        *tail = (*tail + 1) % PTY_QUEUE_SIZE;
    }

    return read;
}

void pty_init(void) {
    memset(g_ptys, 0, sizeof(g_ptys));
    g_pty_dropped_bytes = 0;
    g_pty_invalid_ops = 0;
}

bool pty_is_valid(int pty_id) {
    return pty_id >= 0 && pty_id < PTY_MAX && g_ptys[pty_id].allocated;
}

int pty_alloc(void) {
    for (int i = 0; i < PTY_MAX; i++) {
        if (!g_ptys[i].allocated) {
            g_ptys[i].allocated = true;
            g_ptys[i].m2s_head = 0;
            g_ptys[i].m2s_tail = 0;
            g_ptys[i].s2m_head = 0;
            g_ptys[i].s2m_tail = 0;
            return i;
        }
    }
    return -1;
}

size_t pty_master_write(int pty_id, const uint8_t *buf, size_t len) {
    if (!pty_is_valid(pty_id) || !buf) {
        g_pty_invalid_ops++;
        return 0;
    }
    return pty_queue_write(g_ptys[pty_id].m2s, &g_ptys[pty_id].m2s_head, &g_ptys[pty_id].m2s_tail, buf, len);
}

size_t pty_master_read(int pty_id, uint8_t *buf, size_t len) {
    if (!pty_is_valid(pty_id) || !buf) {
        g_pty_invalid_ops++;
        return 0;
    }
    return pty_queue_read(g_ptys[pty_id].s2m, &g_ptys[pty_id].s2m_head, &g_ptys[pty_id].s2m_tail, buf, len);
}

size_t pty_slave_write(int pty_id, const uint8_t *buf, size_t len) {
    if (!pty_is_valid(pty_id) || !buf) {
        g_pty_invalid_ops++;
        return 0;
    }
    return pty_queue_write(g_ptys[pty_id].s2m, &g_ptys[pty_id].s2m_head, &g_ptys[pty_id].s2m_tail, buf, len);
}

size_t pty_slave_read(int pty_id, uint8_t *buf, size_t len) {
    if (!pty_is_valid(pty_id) || !buf) {
        g_pty_invalid_ops++;
        return 0;
    }
    return pty_queue_read(g_ptys[pty_id].m2s, &g_ptys[pty_id].m2s_head, &g_ptys[pty_id].m2s_tail, buf, len);
}

uint64_t pty_dropped_bytes(void) {
    return g_pty_dropped_bytes;
}

uint64_t pty_invalid_ops(void) {
    return g_pty_invalid_ops;
}
