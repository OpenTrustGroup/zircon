# Copyright 2026 Open Trust Group. All rights reserved.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/stream.cpp \

MODULE_NAME := stream-test

MODULE_STATIC_LIBS := \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/unittest system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
