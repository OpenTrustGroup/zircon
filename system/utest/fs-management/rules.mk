# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := fs

MODULE_SRCS += \
    $(LOCAL_DIR)/fs-management.cpp

MODULE_NAME := fs-management-test

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/fs-test-utils \
    system/ulib/fzl \
    system/ulib/gpt \
    system/ulib/memfs \
    system/ulib/memfs.cpp \
    system/ulib/perftest \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/unittest \

include make/module.mk
