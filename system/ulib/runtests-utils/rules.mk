# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Userspace library.
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_GROUP := test

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/fuchsia-run-test.cpp \
    $(LOCAL_DIR)/log-exporter.cpp \
    $(LOCAL_DIR)/discover-and-run-tests.cpp \
    $(LOCAL_DIR)/runtests-utils.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-logger

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

# zxcpp required for fbl to work.
MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/loader-service \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk

#
# Host library.
#

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
    $(LOCAL_DIR)/posix-run-test.cpp \
    $(LOCAL_DIR)/discover-and-run-tests.cpp \
    $(LOCAL_DIR)/runtests-utils.cpp \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/unittest/include \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk
