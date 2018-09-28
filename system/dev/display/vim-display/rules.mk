# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/edid.cpp \
    $(LOCAL_DIR)/hdmitx.cpp \
    $(LOCAL_DIR)/hdmitx_clk.cpp \
    $(LOCAL_DIR)/registers.cpp \
    $(LOCAL_DIR)/vim-audio.cpp \
    $(LOCAL_DIR)/vim-audio-utils.cpp \
    $(LOCAL_DIR)/vim-display.cpp \
    $(LOCAL_DIR)/vim-spdif-audio-stream.cpp \
    $(LOCAL_DIR)/vpp.cpp \

MODULE_STATIC_LIBS := system/dev/audio/lib/simple-audio-stream \
                      system/ulib/audio-driver-proto \
                      system/ulib/audio-proto-utils \
                      system/ulib/ddk \
                      system/ulib/ddktl \
                      system/ulib/digest \
                      system/ulib/dispatcher-pool \
                      system/ulib/sync \
                      system/ulib/fbl \
                      system/ulib/fzl \
                      system/ulib/hwreg \
                      system/ulib/zx \
                      system/ulib/zxcpp \
                      third_party/ulib/uboringssl \

MODULE_LIBS := system/ulib/driver \
               system/ulib/zircon \
               system/ulib/c

MODULE_HEADER_DEPS := \
    system/dev/lib/amlogic

include make/module.mk
