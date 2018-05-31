# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/zbi.cpp

MODULE_NAME := zbi-test

MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/c

MODULE_STATIC_LIBS += system/ulib/zbi system/ulib/fbl

include make/module.mk
