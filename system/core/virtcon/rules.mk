# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common Code

LOCAL_SRCS := \
    $(LOCAL_DIR)/keyboard-vt100.cpp \
    $(LOCAL_DIR)/keyboard.cpp \
    $(LOCAL_DIR)/vc-device.cpp \
    $(LOCAL_DIR)/vc-gfx.cpp \
    $(LOCAL_DIR)/vc-input.cpp \
    $(LOCAL_DIR)/textcon.cpp \

LOCAL_STATIC_LIBS := \
    system/ulib/gfx \
    system/ulib/hid \
    system/ulib/port \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fzl \
    system/ulib/zx \

LOCAL_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

LOCAL_FIDL_LIBS := \
    system/fidl/fuchsia-display \
    system/fidl/fuchsia-io \

# virtual-console

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/main.cpp $(LOCAL_DIR)/vc-display.cpp

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_FIDL_LIBS := $(LOCAL_FIDL_LIBS)

MODULE_NAME := virtual-console

include make/module.mk


# virtual-console-test

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/keyboard-test.cpp $(LOCAL_DIR)/textcon-test.cpp

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := system/ulib/unittest $(LOCAL_LIBS)

MODULE_FIDL_LIBS := $(LOCAL_FIDL_LIBS)

MODULE_NAME := virtual-console-test

MODULE_DEFINES += BUILD_FOR_TEST=1

include make/module.mk

