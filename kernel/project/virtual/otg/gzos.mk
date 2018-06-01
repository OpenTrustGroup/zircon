# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# modules needed to implement user space

KERNEL_DEFINES += WITH_DEBUG_LINEBUFFER=1
## Directly output kernel message to the console rather than buffering it
#KERNEL_DEFINES += ENABLE_KERNEL_LL_DEBUG=1

MODULES += \
    kernel/lib/userboot \
    kernel/lib/debuglog \
    kernel/lib/ktrace \
    kernel/lib/mtrace \
    kernel/object \
    kernel/syscalls \

MODULES += \
    system/core/devmgr \
    system/core/userboot \
    system/core/svchost \
    system/core/crashsvc \
    system/core/crashanalyzer \

MODULES += \
    system/dev/misc/console \
    system/dev/misc/sysinfo \

MODULES += \
    system/uapp/driverctl \
    system/uapp/psutils \
    system/uapp/dlog \
    system/uapp/runtests \
    system/ulib/syslog \
    system/ulib/virtio \
    third_party/uapp/dash \

MODULES += \
    third_party/lib/sm \

ifeq ($(call TOBOOL,$(DISABLE_UTEST)),false)
MODULES += \
    system/utest/core/smc
endif

EXTRA_BUILDDEPS += $(USER_BOOTDATA)
