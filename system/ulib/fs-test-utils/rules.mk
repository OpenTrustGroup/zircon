# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/fixture.cpp \
    $(LOCAL_DIR)/perftest.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/gpt \
    system/ulib/memfs \
    system/ulib/memfs.cpp \
    system/ulib/perftest \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/unittest \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fs-management \
    system/ulib/sync \
    system/ulib/trace-engine \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk
