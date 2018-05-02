# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS := $(LOCAL_DIR)/main.c

MODULE_FIDL_LIBS := $(LOCAL_DIR)/interfaces

MODULE_STATIC_LIBS := system/ulib/runtime system/ulib/fidl

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
