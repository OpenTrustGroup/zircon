// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <lib/async-loop/cpp/loop.h>
#include <fdio/util.h>
#include <memfs/memfs.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

bool test_memfs_null() {
    BEGIN_TEST;

    async::Loop loop;
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    memfs_filesystem_t* vfs;
    zx_handle_t root;

    ASSERT_EQ(memfs_create_filesystem(loop.async(), &vfs, &root), ZX_OK);
    ASSERT_EQ(zx_handle_close(root), ZX_OK);
    completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

    END_TEST;
}

bool test_memfs_basic() {
    BEGIN_TEST;

    async::Loop loop;
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.async(), &vfs, &root), ZX_OK);
    uint32_t type = PA_FDIO_REMOTE;
    int fd;
    ASSERT_EQ(fdio_create_fd(&root, &type, 1, &fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);

    // Create a file
    const char* filename = "file-a";
    fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd, data, datalen), datalen);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
    ASSERT_EQ(memcmp(buf, data, datalen), 0);

    // Readdir the file
    struct dirent* de;
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);
    completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

    END_TEST;
}

bool test_memfs_install() {
    BEGIN_TEST;

    async::Loop loop;
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.async(), "/mytmp"), ZX_OK);
    int fd = open("/mytmp", O_DIRECTORY | O_RDONLY);
    ASSERT_GE(fd, 0);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);

    // Create a file
    const char* filename = "file-a";
    fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd, data, datalen), datalen);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
    ASSERT_EQ(memcmp(buf, data, datalen), 0);

    // Readdir the file
    struct dirent* de;
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);

    ASSERT_EQ(memfs_install_at(loop.async(), "/mytmp"), ZX_ERR_ALREADY_EXISTS);

    loop.Shutdown();

    // No way to clean up the namespace entry. See ZX-2013 for more details.

    END_TEST;
}

bool test_memfs_close_during_access() {
    BEGIN_TEST;

    async::Loop loop;
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.async(), &vfs, &root), ZX_OK);
    uint32_t type = PA_FDIO_REMOTE;
    int fd;
    ASSERT_EQ(fdio_create_fd(&root, &type, 1, &fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);
    ASSERT_NONNULL(d);
    thrd_t worker;

    struct thread_args {
        DIR* d;
        completion_t spinning{};
    } args {
        .d = d,
    };

    ASSERT_EQ(thrd_create(&worker, [](void* arg) {
        thread_args* args = reinterpret_cast<thread_args*>(arg);
        DIR* d = args->d;
        int fd = openat(dirfd(d), "foo", O_CREAT | O_RDWR);
        while (true) {
            if (close(fd)) {
                return errno == EPIPE ? 0 : -1;
            }

            if ((fd = openat(dirfd(d), "foo", O_RDWR)) < 0) {
                return errno == EPIPE ? 0 : -1;
            }
            completion_signal(&args->spinning);
        }
    }, &args), thrd_success);

    ASSERT_EQ(completion_wait(&args.spinning, ZX_SEC(3)), ZX_OK);

    completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

    int result;
    ASSERT_EQ(thrd_join(worker, &result), thrd_success);
    ASSERT_EQ(result, 0);

    // Now that the filesystem has terminated, we should be
    // unable to access it.
    ASSERT_LT(openat(dirfd(d), "foo", O_CREAT | O_RDWR), 0);
    ASSERT_EQ(errno, EPIPE, "Expected connection to remote server to be closed");

    // Since the filesystem has terminated, this will
    // only close the client side of the connection.
    ASSERT_EQ(closedir(d), 0);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(memfs_tests)
RUN_TEST(test_memfs_null)
RUN_TEST(test_memfs_basic)
RUN_TEST(test_memfs_install)
RUN_TEST(test_memfs_close_during_access)
END_TEST_CASE(memfs_tests)
