# Copyright 2018 Oepn Trust Group
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += $(LOCAL_DIR)/resource.cpp

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \

MODULE_STATIC_LIBS:= \
     system/ulib/fbl \
     system/ulib/zx \

MODULE_PACKAGE := static

include make/module.mk
