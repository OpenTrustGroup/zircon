# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/sysmem.cpp

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-sysmem

MODULE_HEADER_DEPS := \
    system/ulib/svc \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/fidl \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/syslog \
    system/ulib/zircon

include make/module.mk
