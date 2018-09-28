# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := zxio-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/zxio-test.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

MODULE_STATIC_LIBS := \
    system/ulib/zxio \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk
