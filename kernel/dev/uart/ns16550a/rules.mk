# Copyright 2026 Open Trust Group. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/uart.cpp

MODULE_DEPS += \
	kernel/dev/pdev \
	kernel/dev/pdev/uart \

include make/module.mk
