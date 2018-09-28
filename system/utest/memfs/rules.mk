# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_USERTEST_GROUP := fs

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/fidl-tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/memfs-tests.cpp

MODULE_NAME := memfs-test

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/memfs \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
