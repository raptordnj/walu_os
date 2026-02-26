#include <assert.h>
#include <stdio.h>

#include <kernel/fs.h>

int main(void) {
    char buf[256];
    fs_entry_t entries[32];
    fs_entry_t entry;
    size_t count = 0;
    size_t len = 0;
    fs_status_t st;

    fs_init();

    st = fs_pwd(buf, sizeof(buf));
    assert(st == FS_OK);
    assert(buf[0] == '/' && buf[1] == '\0');

    st = fs_mkdir("/docs");
    assert(st == FS_OK);
    st = fs_mkdir("/docs");
    assert(st == FS_ERR_EXISTS);

    st = fs_mkdir_p("/var/log/walu");
    assert(st == FS_OK);
    st = fs_mkdir_p("/var/log/walu");
    assert(st == FS_OK);

    st = fs_stat("/var/log", &entry);
    assert(st == FS_OK);
    assert(entry.is_dir);

    st = fs_touch("/docs/readme.txt");
    assert(st == FS_OK);
    st = fs_write("/docs/readme.txt", "hello", false);
    assert(st == FS_OK);
    st = fs_write("/docs/readme.txt", " world", true);
    assert(st == FS_OK);

    st = fs_read("/docs/readme.txt", buf, sizeof(buf), &len);
    assert(st == FS_OK);
    assert(len == 11);
    assert(buf[0] == 'h' && buf[10] == 'd');

    st = fs_chdir("/docs");
    assert(st == FS_OK);
    st = fs_pwd(buf, sizeof(buf));
    assert(st == FS_OK);
    assert(buf[0] == '/' && buf[1] == 'd');

    st = fs_touch("notes.txt");
    assert(st == FS_OK);

    st = fs_mkdir_p("../tmp/cache");
    assert(st == FS_OK);

    st = fs_touch("/var/log/walu/events.log");
    assert(st == FS_OK);
    st = fs_mkdir_p("/var/log/walu/events.log/archive");
    assert(st == FS_ERR_NOT_DIR);

    st = fs_list(".", entries, sizeof(entries) / sizeof(entries[0]), &count);
    assert(st == FS_OK);
    assert(count >= 2);

    st = fs_chdir("..");
    assert(st == FS_OK);
    st = fs_pwd(buf, sizeof(buf));
    assert(st == FS_OK);
    assert(buf[0] == '/' && buf[1] == '\0');

    st = fs_list("/docs/readme.txt", entries, sizeof(entries) / sizeof(entries[0]), &count);
    assert(st == FS_ERR_NOT_DIR);

    printf("fs host tests passed\n");
    return 0;
}
