// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

static bool stream_read_write_at_test(void) {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(0, 0, &vmo), ZX_OK, "");

    zx::stream stream;
    ASSERT_EQ(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0, &stream),
              ZX_OK, "");

    const char data[] = "abcdef";
    zx_iovec_t write_vector = {
        .buffer = const_cast<char*>(data),
        .capacity = sizeof(data) - 1,
    };
    size_t actual = 0;
    ASSERT_EQ(stream.writev_at(0, 3, &write_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, sizeof(data) - 1, "");

    uint64_t content_size = 0;
    EXPECT_EQ(vmo.get_prop_content_size(&content_size), ZX_OK, "");
    EXPECT_EQ(content_size, 9u, "");

    char out[10] = {};
    zx_iovec_t read_vector = {
        .buffer = out,
        .capacity = sizeof(out),
    };
    ASSERT_EQ(stream.readv_at(0, 0, &read_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, 9u, "");
    EXPECT_EQ(out[0], 0, "");
    EXPECT_EQ(out[1], 0, "");
    EXPECT_EQ(out[2], 0, "");
    EXPECT_EQ(memcmp(out + 3, data, sizeof(data) - 1), 0, "");

    END_TEST;
}

static bool stream_seek_test(void) {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(0, 0, &vmo), ZX_OK, "");

    zx::stream stream;
    ASSERT_EQ(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0, &stream),
              ZX_OK, "");

    const char first[] = "hello";
    zx_iovec_t first_vector = {
        .buffer = const_cast<char*>(first),
        .capacity = sizeof(first) - 1,
    };
    size_t actual = 0;
    ASSERT_EQ(stream.writev(0, &first_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, sizeof(first) - 1, "");

    zx_off_t seek = 0;
    ASSERT_EQ(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 1, &seek), ZX_OK, "");
    EXPECT_EQ(seek, 1u, "");

    const char second[] = "EL";
    zx_iovec_t second_vector = {
        .buffer = const_cast<char*>(second),
        .capacity = sizeof(second) - 1,
    };
    ASSERT_EQ(stream.writev(0, &second_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, sizeof(second) - 1, "");

    char out[6] = {};
    zx_iovec_t read_vector = {
        .buffer = out,
        .capacity = sizeof(out),
    };
    ASSERT_EQ(stream.readv_at(0, 0, &read_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, sizeof(first) - 1, "");
    EXPECT_EQ(memcmp(out, "hELlo", sizeof(first) - 1), 0, "");

    ASSERT_EQ(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -2, &seek), ZX_OK, "");
    EXPECT_EQ(seek, 3u, "");

    END_TEST;
}

static bool stream_append_and_info_test(void) {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(0, 0, &vmo), ZX_OK, "");

    zx::stream stream;
    ASSERT_EQ(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0, &stream),
              ZX_OK, "");

    const char first[] = "aa";
    const char second[] = "bb";
    zx_iovec_t first_vector = {
        .buffer = const_cast<char*>(first),
        .capacity = sizeof(first) - 1,
    };
    zx_iovec_t second_vector = {
        .buffer = const_cast<char*>(second),
        .capacity = sizeof(second) - 1,
    };
    size_t actual = 0;
    ASSERT_EQ(stream.writev(0, &first_vector, 1, &actual), ZX_OK, "");
    ASSERT_EQ(stream.writev(ZX_STREAM_APPEND, &second_vector, 1, &actual), ZX_OK, "");

    zx_info_stream_t info = {};
    size_t actual_count = 0;
    size_t avail_count = 0;
    ASSERT_EQ(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), &actual_count, &avail_count),
              ZX_OK, "");
    EXPECT_EQ(actual_count, 1u, "");
    EXPECT_EQ(avail_count, 1u, "");
    EXPECT_EQ(info.content_size, 4u, "");
    EXPECT_EQ(info.seek, 4u, "");

    char out[5] = {};
    zx_iovec_t read_vector = {
        .buffer = out,
        .capacity = sizeof(out),
    };
    ASSERT_EQ(stream.readv_at(0, 0, &read_vector, 1, &actual), ZX_OK, "");
    EXPECT_EQ(actual, 4u, "");
    EXPECT_EQ(memcmp(out, "aabb", 4), 0, "");

    END_TEST;
}

BEGIN_TEST_CASE(stream_tests)
RUN_TEST(stream_read_write_at_test)
RUN_TEST(stream_seek_test)
RUN_TEST(stream_append_and_info_test)
END_TEST_CASE(stream_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
