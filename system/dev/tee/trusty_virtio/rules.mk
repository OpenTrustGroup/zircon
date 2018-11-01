# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

GLOBAL_INCLUDES += $(LOCAL_DIR)

MODULE_SRCS := \
    $(LOCAL_DIR)/backends/remoteproc.cpp \
    $(LOCAL_DIR)/controller.cpp \
    $(LOCAL_DIR)/ring.cpp \
    $(LOCAL_DIR)/device.cpp \
    $(LOCAL_DIR)/shared_memory.cpp \
    $(LOCAL_DIR)/trusty_vdev.cpp \
    $(LOCAL_DIR)/virtio_driver.cpp \
    $(LOCAL_DIR)/virtio_c.c \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async.default \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/ddk \
    system/ulib/pretty \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/virtio \
    system/ulib/region-alloc \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
