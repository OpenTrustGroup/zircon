# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# The kernel only provides the zx_pager_* syscalls when it is built with
# WITH_USER_PAGER, and system/utest/core/rules.mk wildcards every
# system/utest/core/*/rules.mk into the module list. Gate the whole module so
# this test is built and installed only where it can run.
ifeq (true,$(call TOBOOL,$(WITH_USER_PAGER)))

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/userpager.cpp \
    $(LOCAL_DIR)/test_thread.cpp \
    $(LOCAL_DIR)/pager.cpp \
    $(LOCAL_DIR)/pager-writeback.cpp \
    $(LOCAL_DIR)/main.cpp

MODULE_NAME := pager-test

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/sync \
    system/ulib/inspector \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \
    third_party/ulib/backtrace \
    third_party/ulib/ngunwind

# Disable warnings that are common in imported code
MODULE_COMPILEFLAGS += -Wno-unused-parameter

include make/module.mk

endif
