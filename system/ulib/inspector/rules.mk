# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/backtrace.cpp \
    $(LOCAL_DIR)/dso-list.cpp \
    $(LOCAL_DIR)/registers.cpp \
    $(LOCAL_DIR)/utils.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    third_party/ulib/backtrace \
    third_party/ulib/ngunwind \
    system/ulib/zircon \
    system/ulib/c \

MODULE_PACKAGE := static

# Compile this with frame pointers so that if we crash
# the simplistic unwinder will work.
MODULE_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

include make/module.mk
