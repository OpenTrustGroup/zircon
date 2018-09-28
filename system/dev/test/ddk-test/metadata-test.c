// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <ddk/device.h>
#include <unittest/unittest.h>

extern zx_device_t* ddk_test_dev;

static const char* TEST_STRING = "testing 1 2 3";

static bool add_metadata_test(void) {
    char buffer[32] = {};
    zx_status_t status;
    size_t actual;

    BEGIN_TEST;

    status = device_get_metadata(ddk_test_dev, 1, buffer,sizeof(buffer), &actual);
    ASSERT_EQ(status, ZX_ERR_NOT_FOUND, "device_get_metadata did not return ZX_ERR_NOT_FOUND");

    status = device_add_metadata(ddk_test_dev, 1, TEST_STRING, strlen(TEST_STRING) + 1);
    ASSERT_EQ(status, ZX_OK, "device_add_metadata failed");

    status = device_get_metadata(ddk_test_dev, 1, buffer, sizeof(buffer), &actual);
    ASSERT_EQ(status, ZX_OK, "device_get_metadata failed");
    ASSERT_EQ(actual, strlen(TEST_STRING) + 1, "");
    ASSERT_EQ(strcmp(buffer, TEST_STRING), 0, "");

    END_TEST;
}

static bool publish_metadata_test(void) {
    char buffer[32] = {};
    zx_status_t status;
    size_t actual;

    BEGIN_TEST;
    // This should fail since the path does not match us or our potential children.
    status = device_publish_metadata(ddk_test_dev, "/dev/misc/sysinfo", 2, TEST_STRING,
                                     strlen(TEST_STRING) + 1);
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

    // We are allowed to add metadata to own path.
    status = device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test", 2, TEST_STRING,
                                     strlen(TEST_STRING) + 1);
    ASSERT_EQ(status, ZX_OK, "");

    status = device_get_metadata(ddk_test_dev, 2, buffer, sizeof(buffer), &actual);
    ASSERT_EQ(status, ZX_OK, "device_get_metadata failed");
    ASSERT_EQ(actual, strlen(TEST_STRING) + 1, "");
    ASSERT_EQ(strcmp(buffer, TEST_STRING), 0, "");

    // We are allowed to add metadata to our potential children.
    status = device_publish_metadata(ddk_test_dev, "/dev/test/test/ddk-test/child", 2, TEST_STRING,
                                     strlen(TEST_STRING) + 1);
    ASSERT_EQ(status, ZX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(metadata_tests)
RUN_TEST(add_metadata_test)
RUN_TEST(publish_metadata_test)
END_TEST_CASE(metadata_tests)

struct test_case_element* test_case_ddk_metadata = TEST_CASE_ELEMENT(metadata_tests);
