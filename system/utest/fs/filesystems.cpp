// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fvm/fvm.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/device/ramdisk.h>

#include "filesystems.h"

const char* test_root_path;
bool use_real_disk = false;
char test_disk_path[PATH_MAX];
fs_info_t* test_info;

static char fvm_disk_path[PATH_MAX];

constexpr const char minfs_name[] = "minfs";
constexpr const char memfs_name[] = "memfs";
constexpr const char thinfs_name[] = "thinfs";

const fsck_options_t test_fsck_options = {
    .verbose = false,
    .never_modify = true,
    .always_modify = false,
    .force = true,
};

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

#define TEST_BLOCK_SIZE 512
// This slice size is intentionally somewhat small, so
// we can test increasing the size of a "single-slice"
// inode table. We may want support for tests with configurable
// slice sizes in the future.
#define TEST_FVM_SLICE_SIZE (8 * (1 << 20))

constexpr uint8_t kTestUniqueGUID[] = {
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
constexpr uint8_t kTestPartGUID[] = GUID_DATA_VALUE;

void setup_fs_test(size_t disk_size, fs_test_type_t test_class) {
    test_root_path = MOUNT_PATH;
    int r = mkdir(test_root_path, 0755);
    if ((r < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystem\n");
        exit(-1);
    }

    if (!use_real_disk) {
        size_t block_count = disk_size / TEST_BLOCK_SIZE;
        if (create_ramdisk(TEST_BLOCK_SIZE, block_count, test_disk_path)) {
            fprintf(stderr, "[FAILED]: Could not create ramdisk for test\n");
            exit(-1);
        }
    }

    if (test_class == FS_TEST_FVM) {

        int fd = open(test_disk_path, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "[FAILED]: Could not open test disk\n");
            exit(-1);
        }
        if (fvm_init(fd, TEST_FVM_SLICE_SIZE) != ZX_OK) {
            fprintf(stderr, "[FAILED]: Could not format disk with FVM\n");
            exit(-1);
        }
        if (ioctl_device_bind(fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)) < 0) {
            fprintf(stderr, "[FAILED]: Could not bind disk to FVM driver\n");
            exit(-1);
        }
        snprintf(fvm_disk_path, sizeof(fvm_disk_path), "%s/fvm", test_disk_path);
        if (wait_for_device(fvm_disk_path, ZX_SEC(3)) != ZX_OK) {
            fprintf(stderr, "[FAILED]: FVM driver never appeared at %s\n", test_disk_path);
            exit(-1);
        }

        // Open "fvm" driver
        close(fd);
        int fvm_fd;
        if ((fvm_fd = open(fvm_disk_path, O_RDWR)) < 0) {
            fprintf(stderr, "[FAILED]: Could not open FVM driver\n");
            exit(-1);
        }

        alloc_req_t request;
        memset(&request, 0, sizeof(request));
        request.slice_count = 1;
        strcpy(request.name, "fs-test-partition");
        memcpy(request.type, kTestPartGUID, sizeof(request.type));
        memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

        if ((fd = fvm_allocate_partition(fvm_fd, &request)) < 0) {
            fprintf(stderr, "[FAILED]: Could not allocate FVM partition\n");
            exit(-1);
        }
        close(fvm_fd);
        close(fd);

        if ((fd = open_partition(kTestUniqueGUID, kTestPartGUID, 0, test_disk_path)) < 0) {
            fprintf(stderr, "[FAILED]: Could not locate FVM partition\n");
            exit(-1);
        }
        close(fd);
    }

    if (test_info->mkfs(test_disk_path)) {
        fprintf(stderr, "[FAILED]: Could not format disk (%s) for test\n", test_disk_path);
        exit(-1);
    }

    if (test_info->mount(test_disk_path, test_root_path)) {
        fprintf(stderr, "[FAILED]: Error mounting filesystem\n");
        exit(-1);
    }
}

void teardown_fs_test(fs_test_type_t test_class) {
    if (test_info->unmount(test_root_path)) {
        fprintf(stderr, "[FAILED]: Error unmounting filesystem\n");
        exit(-1);
    }

    if (test_info->fsck(test_disk_path)) {
        fprintf(stderr, "[FAILED]: Filesystem fsck failed\n");
        exit(-1);
    }

    if (test_class == FS_TEST_FVM) {
        // Restore the "fvm_disk_path" to the containing disk, so it can
        // be destroyed when the test completes
        fvm_disk_path[strlen(fvm_disk_path) - strlen("/fvm")] = 0;

        if (use_real_disk) {
            if (fvm_destroy(fvm_disk_path) != ZX_OK) {
                fprintf(stderr, "[FAILED]: Couldn't destroy FVM on test disk\n");
                exit(-1);
            }
        }

        // Move the test_disk_path back to the 'real' disk, rather than
        // a partition within the FVM.
        strcpy(test_disk_path, fvm_disk_path);
    }

    if (!use_real_disk) {
        if (destroy_ramdisk(test_disk_path)) {
            fprintf(stderr, "[FAILED]: Error destroying ramdisk\n");
            exit(-1);
        }
    }
}

// FS-specific functionality:

template <const char* fs_name>
bool should_test_filesystem(void) {
    return !strcmp(filesystem_name_filter, "") || !strcmp(fs_name, filesystem_name_filter);
}

int mkfs_memfs(const char* disk_path) {
    return 0;
}

int fsck_memfs(const char* disk_path) {
    return 0;
}

// TODO(smklein): Even this hacky solution has a hacky implementation, and
// should be replaced with a variation of "rm -r" when ready.
static int unlink_recursive(const char* path) {
    DIR* dir;
    if ((dir = opendir(path)) == NULL) {
        return errno;
    }

    struct dirent* de;
    int r = 0;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        char tmp[PATH_MAX];
        tmp[0] = 0;
        size_t bytes_left = PATH_MAX - 1;
        strncat(tmp, path, bytes_left);
        bytes_left -= strlen(path);
        strncat(tmp, "/", bytes_left);
        bytes_left--;
        strncat(tmp, de->d_name, bytes_left);
        // At the moment, we don't have a great way of identifying what is /
        // isn't a directory. Just try to open it as a directory, and return
        // without an error if we're wrong.
        if ((r = unlink_recursive(tmp)) < 0) {
            break;
        }
        if ((r = unlink(tmp)) < 0) {
            break;
        }
    }

    closedir(dir);
    return r;
}

// TODO(smklein): It would be cleaner to unmount the filesystem completely,
// and remount a fresh copy. However, a hackier (but currently working)
// solution involves recursively deleting all files in the mounted
// filesystem.
int mount_memfs(const char* disk_path, const char* mount_path) {
    struct stat st;
    if (stat(test_root_path, &st)) {
        if (mkdir(test_root_path, 0644) < 0) {
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    int r = unlink_recursive(test_root_path);
    return r;
}

int unmount_memfs(const char* mount_path) {
    return unlink_recursive(test_root_path);
}

int mkfs_minfs(const char* disk_path) {
    zx_status_t status;
    if ((status = mkfs(disk_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                       &default_mkfs_options)) != ZX_OK) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }
    return 0;
}

int fsck_minfs(const char* disk_path) {
    zx_status_t status;
    if ((status = fsck(disk_path, DISK_FORMAT_MINFS, &test_fsck_options, launch_stdio_sync)) != ZX_OK) {
        fprintf(stderr, "fsck on MinFS failed");
        return -1;
    }
    return 0;
}

int mount_minfs(const char* disk_path, const char* mount_path) {
    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open disk: %s\n", disk_path);
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is ready to accept
    // commands.
    zx_status_t status;
    if ((status = mount(fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                        launch_stdio_async)) != ZX_OK) {
        fprintf(stderr, "Could not mount filesystem\n");
        return status;
    }

    return 0;
}

int unmount_minfs(const char* mount_path) {
    zx_status_t status = umount(mount_path);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to unmount filesystem\n");
        return status;
    }
    return 0;
}

bool should_test_thinfs(void) {
    struct stat buf;
    return (stat("/system/bin/thinfs", &buf) == 0) && should_test_filesystem<thinfs_name>();
}

int mkfs_thinfs(const char* disk_path) {
    zx_status_t status;
    if ((status = mkfs(disk_path, DISK_FORMAT_FAT, launch_stdio_sync,
                       &default_mkfs_options)) != ZX_OK) {
        fprintf(stderr, "Could not mkfs filesystem");
        return -1;
    }
    return 0;
}

int fsck_thinfs(const char* disk_path) {
    zx_status_t status;
    if ((status = fsck(disk_path, DISK_FORMAT_FAT, &test_fsck_options, launch_stdio_sync)) != ZX_OK) {
        fprintf(stderr, "fsck on FAT failed");
        return -1;
    }
    return 0;
}

int mount_thinfs(const char* disk_path, const char* mount_path) {
    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open disk: %s\n", disk_path);
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is ready to accept
    // commands.
    zx_status_t status;
    if ((status = mount(fd, mount_path, DISK_FORMAT_FAT, &default_mount_options,
                        launch_stdio_async)) != ZX_OK) {
        fprintf(stderr, "Could not mount filesystem\n");
        return status;
    }

    return 0;
}

int unmount_thinfs(const char* mount_path) {
    zx_status_t status = umount(mount_path);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to unmount filesystem\n");
        return status;
    }
    return 0;
}

fs_info_t FILESYSTEMS[NUM_FILESYSTEMS] = {
    {memfs_name,
        should_test_filesystem<memfs_name>, mkfs_memfs, mount_memfs, unmount_memfs, fsck_memfs,
        .can_be_mounted = false,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = true,
        .supports_create_by_vmo = true,
        .supports_mmap = true,
        .supports_resize = false,
        .nsec_granularity = 1,
    },
    {minfs_name,
        should_test_filesystem<minfs_name>, mkfs_minfs, mount_minfs, unmount_minfs, fsck_minfs,
        .can_be_mounted = true,
        .can_mount_sub_filesystems = true,
        .supports_hardlinks = true,
        .supports_watchers = true,
        .supports_create_by_vmo = false,
        .supports_mmap = false,
        .supports_resize = true,
        .nsec_granularity = 1,
    },
    {thinfs_name,
        should_test_thinfs, mkfs_thinfs, mount_thinfs, unmount_thinfs, fsck_thinfs,
        .can_be_mounted = true,
        .can_mount_sub_filesystems = false,
        .supports_hardlinks = false,
        .supports_watchers = false,
        .supports_create_by_vmo = false,
        .supports_mmap = false,
        .supports_resize = false,
        .nsec_granularity = ZX_SEC(2),
    },
};
