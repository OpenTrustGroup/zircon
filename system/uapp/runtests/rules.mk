# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/runtests.cpp \

MODULE_NAME := runtests

MODULE_STATIC_LIBS := \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/unittest \

include make/module.mk
