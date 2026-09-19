# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build zircon + user space for 64bit arm (cortex-a53)

ARCH := arm64
TARGET := arm64

# Build the user pager: the zx_pager_* syscalls, the page queues, the evictor
# and the scanner, and the pager's 16 bytes per vm_page.
WITH_USER_PAGER := true

include kernel/project/virtual/test.mk
include kernel/project/virtual/user.mk
