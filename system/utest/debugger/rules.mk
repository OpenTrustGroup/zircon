# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := debugger-test

MODULE_SRCS += \
    $(LOCAL_DIR)/debugger.cpp \
    $(LOCAL_DIR)/utils.cpp

MODULE_STATIC_LIBS := \
    system/ulib/runtime \
    system/ulib/fbl \
    system/ulib/zircon-internal

MODULE_LIBS := \
    system/ulib/test-utils \
    system/ulib/unittest \
    system/ulib/launchpad \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
