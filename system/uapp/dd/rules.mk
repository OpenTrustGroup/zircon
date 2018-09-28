# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := dd

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/c

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

include make/module.mk
