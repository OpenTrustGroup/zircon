# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/register-set.cpp \
    $(LOCAL_DIR)/threads.cpp

MODULE_NAME := threads-test

MODULE_LIBS := \
    system/ulib/unittest system/ulib/fdio system/ulib/zircon system/ulib/c
MODULE_STATIC_LIBS := system/ulib/runtime system/utest/core/threads/thread-functions

include make/module.mk
