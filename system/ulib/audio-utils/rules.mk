# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/audio-device-stream.cpp \
    $(LOCAL_DIR)/audio-input.cpp \
    $(LOCAL_DIR)/audio-output.cpp

MODULE_STATIC_LIBS := \
    system/ulib/zx \
    system/ulib/fdio \
    system/ulib/fbl

MODULE_PACKAGE := src

include make/module.mk
