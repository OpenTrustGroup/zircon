# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).server

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/usb-handler.cpp \
    $(LOCAL_DIR)/xdc-server.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fit/include \
    -Isystem/ulib/xdc-host-utils/include \
    -Isystem/ulib/xdc-server-utils/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/xdc-server-utils.hostlib \

MODULE_NAME := xdc-server

MODULE_PACKAGE := bin

include make/module.mk
