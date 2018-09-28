// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include "filesystems.h"
#include "misc.h"

typedef struct {
    // Buffer containing cached messages
    uint8_t buf[fuchsia_io_MAX_BUF];
    // Offset into 'buf' of next message
    uint8_t* ptr;
    // Maximum size of buffer
    size_t size;
} watch_buffer_t;

// Try to read from the channel when it should be empty.
bool check_for_empty(watch_buffer_t* wb, const zx::channel& c) {
    char name[NAME_MAX + 1];
    ASSERT_NULL(wb->ptr);
    ASSERT_EQ(c.read(0, &name, sizeof(name), nullptr, nullptr, 0, nullptr), ZX_ERR_SHOULD_WAIT);
    return true;
}

bool check_local_event(watch_buffer_t* wb, const char* expected, uint8_t event) {
    size_t expected_len = strlen(expected);
    if (wb->ptr != nullptr) {
        // Used a cached event
        ASSERT_EQ(wb->ptr[0], event);
        ASSERT_EQ(wb->ptr[1], expected_len);
        ASSERT_EQ(memcmp(wb->ptr + 2, expected, expected_len), 0);
        wb->ptr = (uint8_t*)((uintptr_t)(wb->ptr) + expected_len + 2);
        ASSERT_LE((uintptr_t)wb->ptr, (uintptr_t) wb->buf + wb->size);
        if ((uintptr_t) wb->ptr == (uintptr_t) wb->buf + wb->size) {
            wb->ptr = nullptr;
        }
        return true;
    }
    return false;
}

// Try to read the 'expected' name off the channel.
bool check_for_event(watch_buffer_t* wb, const zx::channel& c, const char* expected,
                     uint8_t event) {
    if (wb->ptr != nullptr) {
        return check_local_event(wb, expected, event);
    }

    zx_signals_t observed;
    ASSERT_EQ(c.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(5)), &observed), ZX_OK);
    ASSERT_EQ(observed & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
    uint32_t actual;
    ASSERT_EQ(c.read(0, wb->buf, sizeof(wb->buf), &actual, nullptr, 0, nullptr), ZX_OK);
    wb->size = actual;
    wb->ptr = wb->buf;
    return check_local_event(wb, expected, event);
}

bool test_watcher_add(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);
    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    fzl::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
    zx_status_t status;
    ASSERT_EQ(fuchsia_io_DirectoryWatch(caller.borrow_channel(), fuchsia_io_WATCH_MASK_ADDED, 0,
                                        server.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));

    // The channel should be empty
    ASSERT_TRUE(check_for_empty(&wb, client));

    // Creating a file in the directory should trigger the watcher
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_TRUE(check_for_event(&wb, client, "foo", fuchsia_io_WATCH_EVENT_ADDED));

    // Renaming into directory should trigger the watcher
    ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);
    ASSERT_TRUE(check_for_event(&wb, client, "bar", fuchsia_io_WATCH_EVENT_ADDED));

    // Linking into directory should trigger the watcher
    ASSERT_EQ(link("::dir/bar", "::dir/blat"), 0);
    ASSERT_TRUE(check_for_event(&wb, client, "blat", fuchsia_io_WATCH_EVENT_ADDED));

    // Clean up
    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_EQ(unlink("::dir/blat"), 0);

    // There shouldn't be anything else sitting around on the channel
    ASSERT_TRUE(check_for_empty(&wb, client));

    // The fd is still owned by "dir".
    caller.release().release();
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

bool test_watcher_existing(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);

    // Create a couple files in the directory
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    fd = open("::dir/bar", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

    // These files should be visible to the watcher through the "EXISTING"
    // mechanism.
    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    fzl::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
    zx_status_t status;
    uint32_t mask = fuchsia_io_WATCH_MASK_ADDED | fuchsia_io_WATCH_MASK_EXISTING | fuchsia_io_WATCH_MASK_IDLE;
    ASSERT_EQ(fuchsia_io_DirectoryWatch(caller.borrow_channel(), mask, 0,
                                        server.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));

    // The channel should see the contents of the directory
    ASSERT_TRUE(check_for_event(&wb, client, ".", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, client, "foo", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, client, "bar", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, client, "", fuchsia_io_WATCH_EVENT_IDLE));
    ASSERT_TRUE(check_for_empty(&wb, client));

    // Now, if we choose to add additional files, they'll show up separately
    // with an "ADD" event.
    fd = open("::dir/baz", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_TRUE(check_for_event(&wb, client, "baz", fuchsia_io_WATCH_EVENT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, client));

    // If we create a secondary watcher with the "EXISTING" request, we'll
    // see all files in the directory, but the first watcher won't see anything.
    zx::channel client2;
    ASSERT_EQ(zx::channel::create(0, &client2, &server), ZX_OK);
    ASSERT_EQ(fuchsia_io_DirectoryWatch(caller.borrow_channel(), mask, 0,
                                        server.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    watch_buffer_t wb2;
    memset(&wb2, 0, sizeof(wb2));
    ASSERT_TRUE(check_for_event(&wb2, client2, ".", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, client2, "foo", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, client2, "bar", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, client2, "baz", fuchsia_io_WATCH_EVENT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, client2, "", fuchsia_io_WATCH_EVENT_IDLE));
    ASSERT_TRUE(check_for_empty(&wb2, client2));
    ASSERT_TRUE(check_for_empty(&wb, client));

    // Clean up
    ASSERT_EQ(unlink("::dir/foo"), 0);
    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_EQ(unlink("::dir/baz"), 0);

    // There shouldn't be anything else sitting around on either channel
    ASSERT_TRUE(check_for_empty(&wb, client));
    ASSERT_TRUE(check_for_empty(&wb2, client2));

    // The fd is still owned by "dir".
    caller.release().release();
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

bool test_watcher_removed(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    uint32_t mask = fuchsia_io_WATCH_MASK_ADDED | fuchsia_io_WATCH_MASK_REMOVED;
    zx_status_t status;
    fzl::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
    ASSERT_EQ(fuchsia_io_DirectoryWatch(caller.borrow_channel(), mask, 0,
                                        server.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));

    ASSERT_TRUE(check_for_empty(&wb, client));

    int fd = openat(dirfd(dir), "foo", O_CREAT | O_RDWR | O_EXCL);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

    ASSERT_TRUE(check_for_event(&wb, client, "foo", fuchsia_io_WATCH_EVENT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, client));

    ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);

    ASSERT_TRUE(check_for_event(&wb, client, "foo", fuchsia_io_WATCH_EVENT_REMOVED));
    ASSERT_TRUE(check_for_event(&wb, client, "bar", fuchsia_io_WATCH_EVENT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, client));

    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_TRUE(check_for_event(&wb, client, "bar", fuchsia_io_WATCH_EVENT_REMOVED));
    ASSERT_TRUE(check_for_empty(&wb, client));

    // The fd is still owned by "dir".
    caller.release().release();
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(directory_watcher_tests,
    RUN_TEST_MEDIUM(test_watcher_add)
    RUN_TEST_MEDIUM(test_watcher_existing)
    RUN_TEST_MEDIUM(test_watcher_removed)
)
