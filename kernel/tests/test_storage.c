#include <assert.h>
#include <stdio.h>

#include <kernel/fs.h>
#include <kernel/storage.h>
#include <kernel/string.h>

int main(void) {
    storage_device_info_t info;
    fs_entry_t entry;
    char buf[512];
    size_t len = 0;
    storage_status_t st;

    fs_init();
    storage_init();

    assert(storage_device_count() >= 2);
    assert(storage_find_device("/dev/ram0", &info));
    assert(info.formatted);
    assert(storage_find_device("/dev/usb0", &info));
    assert(!info.formatted);

    st = storage_mount("/dev/usb0", "/media/usb0", false, false, false, false);
    assert(st == STORAGE_ERR_NO_FILESYSTEM);

    st = storage_install("/dev/usb0", "/mnt/usb0", true, false, true);
    assert(st == STORAGE_ERR_NO_FILESYSTEM);

    st = storage_format("/dev/usb0", "ext4", "DATA", true, true, true);
    assert(st == STORAGE_OK);
    assert(storage_find_device("/dev/usb0", &info));
    assert(!info.formatted);

    st = storage_format("/dev/usb0", "ext4", "DATA", true, false, true);
    assert(st == STORAGE_OK);
    assert(storage_find_device("/dev/usb0", &info));
    assert(info.formatted);

    st = storage_mount("/dev/usb0", "/media/usb0", true, false, false, false);
    assert(st == STORAGE_ERR_POLICY);

    st = storage_install("/dev/usb0", "/mnt/usb0", true, false, false);
    assert(st == STORAGE_ERR_CONFIRMATION_REQUIRED);
    st = storage_install("/dev/usb0", "/mnt/usb0", true, true, true);
    assert(st == STORAGE_OK);
    assert(fs_stat("/mnt/usb0/etc/passwd", &entry) == FS_ERR_NOT_FOUND);

    st = storage_install("/dev/usb0", "/mnt/usb0", true, false, true);
    assert(st == STORAGE_OK);
    assert(fs_stat("/mnt/usb0/bin", &entry) == FS_OK);
    assert(entry.is_dir);
    assert(fs_stat("/mnt/usb0/etc", &entry) == FS_OK);
    assert(entry.is_dir);
    assert(fs_stat("/mnt/usb0/home/walu", &entry) == FS_OK);
    assert(entry.is_dir);
    assert(fs_read("/mnt/usb0/etc/passwd", buf, sizeof(buf), &len) == FS_OK);
    assert(len > 10);
    assert(strncmp(buf, "root:x:0:0", 10) == 0);

    assert(storage_find_device("/dev/usb0", &info));
    assert(strcmp(info.mountpoint, "/mnt/usb0") == 0);

    st = storage_umount_target("/mnt/usb0", false);
    assert(st == STORAGE_OK);
    st = storage_mount("/dev/usb0", "/mnt/usb0", false, false, false, false);
    assert(st == STORAGE_OK);
    st = storage_install("/dev/usb0", "/mnt/usb0", true, false, true);
    assert(st == STORAGE_ERR_POLICY);
    st = storage_umount_target("/mnt/usb0", false);
    assert(st == STORAGE_OK);

    st = storage_mount("/dev/usb0", "/media/usb0", false, false, false, false);
    assert(st == STORAGE_OK);
    assert(storage_find_device("/dev/usb0", &info));
    assert(info.mountpoint[0] != '\0');

    st = storage_fsck("/dev/usb0", false, false, true);
    assert(st == STORAGE_ERR_BUSY);

    st = storage_umount_target("/media/usb0", false);
    assert(st == STORAGE_OK);

    st = storage_fsck("/dev/usb0", true, false, false);
    assert(st == STORAGE_ERR_CONFIRMATION_REQUIRED);
    st = storage_fsck("/dev/usb0", true, false, true);
    assert(st == STORAGE_OK);

    st = storage_mount("/dev/usb0", "/media/usb0", false, false, false, false);
    assert(st == STORAGE_OK);
    st = storage_format("/dev/usb0", "vfat", "VFATDATA", true, false, true);
    assert(st == STORAGE_ERR_BUSY);
    st = storage_umount_target("/dev/usb0", false);
    assert(st == STORAGE_OK);

    st = storage_format("/dev/usb0", "vfat", "VFATDATA", true, false, true);
    assert(st == STORAGE_OK);
    assert(storage_find_device("/dev/usb0", &info));
    assert(info.formatted);
    assert(info.fstype[0] != '\0');

    printf("storage host tests passed\n");
    return 0;
}
