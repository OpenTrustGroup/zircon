# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := fixfs

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    third_party/ulib/cksum \

MODULE_LIBS := \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

include make/module.mk
