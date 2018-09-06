# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/context.cpp \
    $(LOCAL_DIR)/context_api.cpp \
    $(LOCAL_DIR)/engine.cpp \
    $(LOCAL_DIR)/nonce.cpp

MODULE_EXPORT := so
MODULE_SO_NAME := trace-engine

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
