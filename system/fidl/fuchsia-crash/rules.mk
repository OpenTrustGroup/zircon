# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := fidl

MODULE_PACKAGE := fidl

MODULE_FIDL_LIBRARY := fuchsia.crash

MODULE_FIDL_DEPS := system/fidl/fuchsia-mem

MODULE_SRCS += $(LOCAL_DIR)/crash.fidl

include make/module.mk
