# Copyright 2026 Open Trust Group. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The hypervisor (HEE) of Nebula's venus system on the QEMU board: the
# project garnet's venus-hee build reads, with the user-space interfaces of
# GRT's zircon that open-ddk publishes. Its modules are the ones GRT's
# venus-hee project names that boot on QEMU, and the libraries garnet links,
# rather than every module in the tree.

ARCH := arm64
TARGET := arm64

WITH_USER_PAGER := true

include kernel/project/virtual/test.mk

KERNEL_DEFINES += WITH_DEBUG_LINEBUFFER=1

# garnet's appmgr serves the service directory itself on the directory request
# devmgr gives it, not as "svc" within it.
USER_DEFINES += DEVMGR_APPMGR_SERVES_SVC=1

MODULES += \
    kernel/lib/userboot \
    kernel/lib/debuglog \
    kernel/lib/ktrace \
    kernel/lib/mtrace \
    kernel/object \
    kernel/syscalls \

MODULES += \
    system/core/crashlogger \
    system/core/crashsvc \
    system/core/devmgr \
    system/core/userboot \

MODULES += \
    system/dev/misc/builtin \
    system/dev/misc/console \
    system/dev/misc/cpufreq \
    system/dev/misc/ktrace \
    system/dev/misc/sysinfo \

MODULES += \
    system/uapp/dlog \
    system/uapp/driverctl \
    system/uapp/fs-fsck \
    system/uapp/fs-mkfs \
    system/uapp/fs-mount \
    system/uapp/fs-umount \
    system/uapp/iochk \
    system/uapp/psutils \
    system/uapp/runtests \
    system/uapp/traceme \
    third_party/uapp/dash \

# The user libraries garnet's venus-hee build links.
MODULES += \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-testutils \
    system/ulib/audio-proto-utils \
    system/ulib/audio-utils \
    system/ulib/bitmap \
    system/ulib/block-client \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/dispatcher-pool \
    system/ulib/driver \
    system/ulib/fbl \
    system/ulib/fdio \
    system/ulib/fidl \
    system/ulib/fs \
    system/ulib/fs-management \
    system/ulib/hid \
    system/ulib/hid-parser \
    system/ulib/launchpad \
    system/ulib/loader-service \
    system/ulib/mdi \
    system/ulib/pretty \
    system/ulib/region-alloc \
    system/ulib/sync \
    system/ulib/syslog \
    system/ulib/trace \
    system/ulib/trace-engine \
    system/ulib/trace-provider \
    system/ulib/trace-reader \
    system/ulib/virtio \
    system/ulib/vmofs \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/backtrace \
    third_party/ulib/linenoise \
    third_party/ulib/ngunwind \

EXTRA_BUILDDEPS += $(USER_BOOTDATA)
