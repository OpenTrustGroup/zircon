// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <unistd.h>
#include <lib/fdio/limits.h>

#include <unittest/unittest.h>

bool sysconf_test(void) {
    BEGIN_TEST;

    int max = sysconf(_SC_OPEN_MAX);
    ASSERT_EQ(max, FDIO_MAX_FD, "sysconf(_SC_OPEN_MAX) != FDIO_MAX_FD");

    END_TEST;
}

BEGIN_TEST_CASE(fdio_open_max_test)
RUN_TEST(sysconf_test);
END_TEST_CASE(fdio_open_max_test)

