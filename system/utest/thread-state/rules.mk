# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += $(LOCAL_DIR)/thread-state.cpp

MODULE_NAME := thread-state-test

MODULE_STATIC_LIBS := \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/test-utils \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
