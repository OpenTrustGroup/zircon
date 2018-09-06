# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/mmc.c \
    $(LOCAL_DIR)/ops.c \
    $(LOCAL_DIR)/sd.c \
    $(LOCAL_DIR)/sdmmc.c \
    $(LOCAL_DIR)/sdio.c \
    $(LOCAL_DIR)/sdio-interrupts.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \
    system/ulib/pretty \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx

MODULE_LIBS := system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/async.default \
    system/ulib/trace-engine

include make/module.mk
