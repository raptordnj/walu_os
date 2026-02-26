#include <assert.h>
#include <stdio.h>

#include <kernel/storage.h>

int main(void) {
    storage_device_info_t info;
    storage_status_t st;

    storage_init();

    assert(storage_device_count() >= 2);
    assert(storage_find_device("/dev/ram0", &info));
    assert(info.formatted);
    assert(storage_find_device("/dev/usb0", &info));
    assert(!info.formatted);

    st = storage_mount("/dev/usb0", "/media/usb0", false, false, false, false);
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
