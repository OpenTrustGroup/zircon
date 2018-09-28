# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/astro.c \
    $(LOCAL_DIR)/astro-bluetooth.c \
    $(LOCAL_DIR)/astro-gpio.c \
    $(LOCAL_DIR)/astro-i2c.c \
    $(LOCAL_DIR)/astro-usb.c \
    $(LOCAL_DIR)/astro-display.c \
    $(LOCAL_DIR)/astro-touch.c \
    $(LOCAL_DIR)/astro-rawnand.c \
    $(LOCAL_DIR)/astro-sdio.c \
    $(LOCAL_DIR)/astro-canvas.c \
    $(LOCAL_DIR)/astro-light.c \
    $(LOCAL_DIR)/astro-thermal.c \
    $(LOCAL_DIR)/astro-video.c \
    $(LOCAL_DIR)/astro-clk.c \
    $(LOCAL_DIR)/astro-audio.c \
    $(LOCAL_DIR)/astro-tee.c \

MODULE_STATIC_LIBS := \
    system/dev/lib/amlogic \
    system/ulib/ddk \
    system/ulib/sync \
    system/dev/lib/broadcom \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
