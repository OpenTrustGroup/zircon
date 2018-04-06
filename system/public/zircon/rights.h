// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

// Default rights for objects.
#define ZX_DEFAULT_CHANNEL_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_DUPLICATE)) |\
     ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER)

#define ZX_DEFAULT_EVENT_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_EVENT_PAIR_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO |\
     ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER)

#define ZX_DEFAULT_FIFO_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO |\
     ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER)

#define ZX_DEFAULT_GUEST_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE)

#define ZX_DEFAULT_INTERRUPT_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_DUPLICATE)) | ZX_RIGHTS_IO)

#define ZX_DEFAULT_IO_MAPPING_RIGHTS \
    (ZX_RIGHT_READ | ZX_RIGHT_INSPECT)

#define ZX_DEFAULT_JOB_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY |\
     ZX_RIGHTS_POLICY | ZX_RIGHT_ENUMERATE | ZX_RIGHT_DESTROY |\
     ZX_RIGHT_SIGNAL | ZX_RIGHT_MANAGE_JOB)

#define ZX_DEFAULT_LOG_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_SMC_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_PCI_DEVICE_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO)

#define ZX_DEFAULT_PCI_INTERRUPT_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_DUPLICATE)) | ZX_RIGHTS_IO)

#define ZX_DEFAULT_PORT_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_WAIT)) | ZX_RIGHTS_IO)

#define ZX_DEFAULT_PROCESS_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY |\
     ZX_RIGHT_ENUMERATE | ZX_RIGHT_DESTROY | ZX_RIGHT_SIGNAL |\
     ZX_RIGHT_MANAGE_PROCESS)

#define ZX_DEFAULT_RESOURCE_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_SOCKET_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER)

#define ZX_DEFAULT_THREAD_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY |\
     ZX_RIGHT_DESTROY | ZX_RIGHT_SIGNAL | ZX_RIGHT_MANAGE_THREAD)

#define ZX_DEFAULT_TIMERS_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_VCPU_RIGHTS \
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_EXECUTE | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_VMAR_RIGHTS \
    (ZX_RIGHTS_BASIC)

#define ZX_DEFAULT_VMO_RIGHTS\
    (ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY |\
     ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP | ZX_RIGHT_SIGNAL)

#define ZX_DEFAULT_IOMMU_RIGHTS \
    (ZX_RIGHTS_BASIC & (~ZX_RIGHT_WAIT))

#define ZX_DEFAULT_BTI_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_WAIT)) | ZX_RIGHT_READ | ZX_RIGHT_MAP)

#define ZX_DEFAULT_PROFILE_RIGHTS \
    ((ZX_RIGHTS_BASIC & (~ZX_RIGHT_WAIT)) | ZX_RIGHT_APPLY_PROFILE)
