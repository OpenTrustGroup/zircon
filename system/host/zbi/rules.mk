# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/zbi.cpp \

MODULE_COMPILEFLAGS := \
    -Ithird_party/ulib/lz4/include \
    -Ithird_party/ulib/cksum/include \
    -Isystem/ulib/fbl/include \

MODULE_HOST_LIBS := \
    third_party/ulib/lz4.hostlib \
    third_party/ulib/cksum.hostlib \
    system/ulib/fbl.hostlib \

MODULE_PACKAGE := bin

include make/module.mk
