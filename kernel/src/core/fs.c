#include <kernel/fs.h>
#include <kernel/string.h>

#define FS_MAX_NODES 128
#define FS_MAX_NAME 31
#define FS_MAX_CONTENT 512
#define FS_MAX_DEPTH 32

typedef struct {
    bool in_use;
    bool is_dir;
    int parent;
    char name[FS_MAX_NAME + 1];
    char content[FS_MAX_CONTENT];
    size_t size;
} fs_node_t;

static fs_node_t g_nodes[FS_MAX_NODES];
static int g_cwd = 0;

static size_t fs_strnlen_local(const char *s, size_t max_len) {
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (n < max_len && s[n] != '\0') {
        n++;
    }
    return n;
}

static void fs_copy(char *dst, size_t dst_len, const char *src) {
    size_t n;
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = fs_strnlen_local(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool fs_name_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int fs_find_child(int parent, const char *name) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!g_nodes[i].in_use) {
            continue;
        }
        if (g_nodes[i].parent == parent && fs_name_eq(g_nodes[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static fs_status_t fs_step_component(int *cur, const char *comp) {
    int next;
    if (fs_name_eq(comp, ".")) {
        return FS_OK;
    }
    if (fs_name_eq(comp, "..")) {
        *cur = g_nodes[*cur].parent;
        return FS_OK;
    }
    next = fs_find_child(*cur, comp);
    if (next < 0) {
        return FS_ERR_NOT_FOUND;
    }
    *cur = next;
    return FS_OK;
}

static fs_status_t fs_parse_component(const char *path, size_t *i, char *out, size_t out_len) {
    size_t n = 0;
    while (path[*i] != '\0' && path[*i] != '/') {
        if (n + 1 >= out_len) {
            return FS_ERR_INVALID;
        }
        out[n++] = path[*i];
        (*i)++;
    }
    if (n == 0) {
        return FS_ERR_INVALID;
    }
    out[n] = '\0';
    return FS_OK;
}

static fs_status_t fs_resolve(const char *path, int *out_idx) {
    char comp[FS_MAX_NAME + 1];
    size_t i = 0;
    int cur;
    fs_status_t st;

    if (!out_idx) {
        return FS_ERR_INVALID;
    }
    if (!path || path[0] == '\0') {
        *out_idx = g_cwd;
        return FS_OK;
    }

    cur = (path[0] == '/') ? 0 : g_cwd;
    if (path[0] == '/') {
        i = 1;
    }

    while (1) {
        while (path[i] == '/') {
            i++;
        }
        if (path[i] == '\0') {
            *out_idx = cur;
            return FS_OK;
        }

        st = fs_parse_component(path, &i, comp, sizeof(comp));
        if (st != FS_OK) {
            return st;
        }

        st = fs_step_component(&cur, comp);
        if (st != FS_OK) {
            return st;
        }
    }
}

static fs_status_t fs_resolve_parent(const char *path, int *out_parent, char *out_name, size_t out_name_len) {
    char comp[FS_MAX_NAME + 1];
    size_t i = 0;
    int cur;
    fs_status_t st;

    if (!path || !out_parent || !out_name || out_name_len == 0 || path[0] == '\0') {
        return FS_ERR_INVALID;
    }

    cur = (path[0] == '/') ? 0 : g_cwd;
    if (path[0] == '/') {
        i = 1;
    }

    while (1) {
        size_t j;
        while (path[i] == '/') {
            i++;
        }
        if (path[i] == '\0') {
            return FS_ERR_INVALID;
        }

        st = fs_parse_component(path, &i, comp, sizeof(comp));
        if (st != FS_OK) {
            return st;
        }

        j = i;
        while (path[j] == '/') {
            j++;
        }
        if (path[j] == '\0') {
            if (fs_name_eq(comp, ".") || fs_name_eq(comp, "..")) {
                return FS_ERR_INVALID;
            }
            *out_parent = cur;
            fs_copy(out_name, out_name_len, comp);
            return FS_OK;
        }

        st = fs_step_component(&cur, comp);
        if (st != FS_OK) {
            return st;
        }
        if (!g_nodes[cur].is_dir) {
            return FS_ERR_NOT_DIR;
        }
    }
}

static int fs_alloc_node(void) {
    for (int i = 1; i < FS_MAX_NODES; i++) {
        if (!g_nodes[i].in_use) {
            return i;
        }
    }
    return -1;
}

static fs_status_t fs_create_node(const char *path, bool is_dir, int *out_idx) {
    int parent;
    int idx;
    char name[FS_MAX_NAME + 1];
    fs_status_t st;

    st = fs_resolve_parent(path, &parent, name, sizeof(name));
    if (st != FS_OK) {
        return st;
    }
    if (!g_nodes[parent].is_dir) {
        return FS_ERR_NOT_DIR;
    }
    if (fs_find_child(parent, name) >= 0) {
        return FS_ERR_EXISTS;
    }

    idx = fs_alloc_node();
    if (idx < 0) {
        return FS_ERR_NO_SPACE;
    }

    memset(&g_nodes[idx], 0, sizeof(g_nodes[idx]));
    g_nodes[idx].in_use = true;
    g_nodes[idx].is_dir = is_dir;
    g_nodes[idx].parent = parent;
    fs_copy(g_nodes[idx].name, sizeof(g_nodes[idx].name), name);
    g_nodes[idx].size = 0;
    g_nodes[idx].content[0] = '\0';

    if (out_idx) {
        *out_idx = idx;
    }
    return FS_OK;
}

static void fs_seed_dirs(void) {
    (void)fs_mkdir("/home");
    (void)fs_mkdir("/tmp");
    (void)fs_mkdir("/media");
    (void)fs_mkdir("/media/usb0");
}

void fs_init(void) {
    memset(g_nodes, 0, sizeof(g_nodes));
    g_nodes[0].in_use = true;
    g_nodes[0].is_dir = true;
    g_nodes[0].parent = 0;
    fs_copy(g_nodes[0].name, sizeof(g_nodes[0].name), "/");
    g_nodes[0].size = 0;
    g_nodes[0].content[0] = '\0';
    g_cwd = 0;
    fs_seed_dirs();
}

fs_status_t fs_pwd(char *out, size_t cap) {
    int stack[FS_MAX_DEPTH];
    int depth = 0;
    int cur = g_cwd;
    size_t pos = 0;

    if (!out || cap == 0) {
        return FS_ERR_INVALID;
    }

    if (g_cwd == 0) {
        if (cap < 2) {
            return FS_ERR_NO_SPACE;
        }
        out[0] = '/';
        out[1] = '\0';
        return FS_OK;
    }

    while (cur != 0) {
        if (depth >= FS_MAX_DEPTH) {
            return FS_ERR_NO_SPACE;
        }
        stack[depth++] = cur;
        cur = g_nodes[cur].parent;
    }

    if (pos + 1 >= cap) {
        return FS_ERR_NO_SPACE;
    }
    out[pos++] = '/';

    for (int i = depth - 1; i >= 0; i--) {
        const char *name = g_nodes[stack[i]].name;
        size_t n = strlen(name);
        if (pos + n + (i > 0 ? 1u : 0u) >= cap) {
            return FS_ERR_NO_SPACE;
        }
        memcpy(out + pos, name, n);
        pos += n;
        if (i > 0) {
            out[pos++] = '/';
        }
    }

    out[pos] = '\0';
    return FS_OK;
}

fs_status_t fs_chdir(const char *path) {
    int idx;
    fs_status_t st = fs_resolve(path, &idx);
    if (st != FS_OK) {
        return st;
    }
    if (!g_nodes[idx].is_dir) {
        return FS_ERR_NOT_DIR;
    }
    g_cwd = idx;
    return FS_OK;
}

fs_status_t fs_mkdir(const char *path) {
    return fs_create_node(path, true, 0);
}

fs_status_t fs_touch(const char *path) {
    int idx;
    fs_status_t st = fs_resolve(path, &idx);
    if (st == FS_OK) {
        if (g_nodes[idx].is_dir) {
            return FS_ERR_IS_DIR;
        }
        return FS_OK;
    }
    if (st != FS_ERR_NOT_FOUND) {
        return st;
    }
    return fs_create_node(path, false, 0);
}

fs_status_t fs_write(const char *path, const char *data, bool append) {
    int idx;
    size_t existing = 0;
    size_t n = data ? strlen(data) : 0;
    fs_status_t st = fs_resolve(path, &idx);

    if (st == FS_ERR_NOT_FOUND) {
        st = fs_create_node(path, false, &idx);
    }
    if (st != FS_OK) {
        return st;
    }
    if (g_nodes[idx].is_dir) {
        return FS_ERR_IS_DIR;
    }

    if (append) {
        existing = g_nodes[idx].size;
    }

    if (existing + n >= FS_MAX_CONTENT) {
        return FS_ERR_NO_SPACE;
    }

    if (!append) {
        g_nodes[idx].size = 0;
        g_nodes[idx].content[0] = '\0';
        existing = 0;
    }

    if (n > 0) {
        memcpy(g_nodes[idx].content + existing, data, n);
    }
    g_nodes[idx].size = existing + n;
    g_nodes[idx].content[g_nodes[idx].size] = '\0';
    return FS_OK;
}

fs_status_t fs_read(const char *path, char *out, size_t cap, size_t *out_len) {
    int idx;
    size_t n;
    fs_status_t st;

    if (!out || cap == 0) {
        return FS_ERR_INVALID;
    }

    st = fs_resolve(path, &idx);
    if (st != FS_OK) {
        return st;
    }
    if (g_nodes[idx].is_dir) {
        return FS_ERR_IS_DIR;
    }

    n = g_nodes[idx].size;
    if (n + 1 > cap) {
        return FS_ERR_NO_SPACE;
    }

    if (n > 0) {
        memcpy(out, g_nodes[idx].content, n);
    }
    out[n] = '\0';
    if (out_len) {
        *out_len = n;
    }
    return FS_OK;
}

fs_status_t fs_list(const char *path, fs_entry_t *entries, size_t max_entries, size_t *out_count) {
    int dir = 0;
    size_t count = 0;
    fs_status_t st;

    if (path && path[0] != '\0') {
        st = fs_resolve(path, &dir);
        if (st != FS_OK) {
            return st;
        }
    } else {
        dir = g_cwd;
    }

    if (!g_nodes[dir].is_dir) {
        return FS_ERR_NOT_DIR;
    }

    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!g_nodes[i].in_use) {
            continue;
        }
        if (i == 0 && dir != 0) {
            continue;
        }
        if (g_nodes[i].parent != dir) {
            continue;
        }
        if (entries && count < max_entries) {
            memset(&entries[count], 0, sizeof(entries[count]));
            fs_copy(entries[count].name, sizeof(entries[count].name), g_nodes[i].name);
            entries[count].is_dir = g_nodes[i].is_dir;
            entries[count].size = g_nodes[i].size;
        }
        count++;
    }

    if (out_count) {
        *out_count = count;
    }
    if (entries && count > max_entries) {
        return FS_ERR_NO_SPACE;
    }
    return FS_OK;
}

const char *fs_status_string(fs_status_t status) {
    switch (status) {
        case FS_OK: return "ok";
        case FS_ERR_NOT_FOUND: return "not-found";
        case FS_ERR_EXISTS: return "already-exists";
        case FS_ERR_NOT_DIR: return "not-directory";
        case FS_ERR_IS_DIR: return "is-directory";
        case FS_ERR_INVALID: return "invalid-args";
        case FS_ERR_NO_SPACE: return "no-space";
        default: return "unknown";
    }
}
