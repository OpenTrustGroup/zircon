// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// Give the CPU frequency driver a buffer for its trace records: the
// buffer's physical address and size, two uint64_t.
#define IOCTL_CPUFREQ_TRACE_SET_BUF \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CPUFREQ, 1)

static inline ssize_t ioctl_cpufreq_trace_set_buf(int fd, const uint64_t* pa_and_size) {
    return fdio_ioctl(fd, IOCTL_CPUFREQ_TRACE_SET_BUF, pa_and_size, sizeof(uint64_t) * 2,
                      NULL, 0);
}
