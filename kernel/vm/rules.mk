# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
    kernel/lib/fbl \
    kernel/lib/pretty \
    kernel/lib/user_copy \
    third_party/lib/cryptolib

MODULE_SRCS += \
    $(LOCAL_DIR)/bootalloc.cpp \
    $(LOCAL_DIR)/bootreserve.cpp \
    $(LOCAL_DIR)/kstack.cpp \
    $(LOCAL_DIR)/page.cpp \
    $(LOCAL_DIR)/pmm.cpp \
    $(LOCAL_DIR)/pmm_arena.cpp \
    $(LOCAL_DIR)/pmm_node.cpp \
    $(LOCAL_DIR)/vm.cpp \
    $(LOCAL_DIR)/vm_address_region.cpp \
    $(LOCAL_DIR)/vm_address_region_or_mapping.cpp \
    $(LOCAL_DIR)/vm_aspace.cpp \
    $(LOCAL_DIR)/vm_mapping.cpp \
    $(LOCAL_DIR)/vm_object.cpp \
    $(LOCAL_DIR)/vm_object_paged.cpp \
    $(LOCAL_DIR)/vm_object_physical.cpp \
    $(LOCAL_DIR)/vm_page_list.cpp \
    $(LOCAL_DIR)/vm_unittest.cpp \
    $(LOCAL_DIR)/vmm.cpp \
    $(LOCAL_DIR)/page_source.cpp \

# The page queues, the evictor and the scanner serve the user pager and nothing
# else: no page is pager-backed without it, so the queues stay empty, the
# evictor has nothing to reclaim and the scanner's only remaining job is to
# enable that evictor. They leave the build together, replaced by the empty
# method bodies in page_queues_disabled.cpp so that vm_page_list.cpp and
# vm_object_paged.cpp need no #ifdef.
ifeq (true,$(call TOBOOL,$(WITH_USER_PAGER)))

MODULE_SRCS += \
    $(LOCAL_DIR)/evictor.cpp \
    $(LOCAL_DIR)/page_queues.cpp \
    $(LOCAL_DIR)/scanner.cpp \

else

MODULE_SRCS += \
    $(LOCAL_DIR)/page_queues_disabled.cpp \

endif

include make/module.mk
