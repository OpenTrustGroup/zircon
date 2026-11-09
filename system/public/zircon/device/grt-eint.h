// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Request the interrupt of an external interrupt (EINT) pin for a guest. The
// input is a pdev_eint_req_t and the output is the interrupt handle.
#define IOCTL_GRT_EINT_IRQ_REQUEST \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_EINT, 0)

// Set the trigger type of a pin; the input is a pdev_eint_set_type_t.
#define IOCTL_GRT_EINT_SET_TYPE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 1)

// Mask, unmask or acknowledge a pin; the input is a pdev_eint_op_t.
#define IOCTL_GRT_EINT_MASK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 2)
#define IOCTL_GRT_EINT_UNMASK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 3)
#define IOCTL_GRT_EINT_ACK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 4)

// Return the number of EINT pins as one uint32_t.
#define IOCTL_GRT_EINT_GET_MAX_EINT_NUM \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 5)

// Release every pin a guest holds. The input is the guest's VM ID; the driver
// reads its low byte.
#define IOCTL_GRT_EINT_VM_DESTROY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_EINT, 6)

typedef struct pdev_eint_req {
    uint32_t eint_num;
    uint16_t vmid;
    uint16_t reserved;
} pdev_eint_req_t;

typedef struct pdev_eint_set_type {
    uint32_t eint_num;
    uint32_t type;
} pdev_eint_set_type_t;

typedef struct pdev_eint_op {
    uint32_t eint_num;
} pdev_eint_op_t;

// ssize_t ioctl_grt_eint_irq_request(int fd, const pdev_eint_req_t* in,
//                                    zx_handle_t* out);
IOCTL_WRAPPER_INOUT(ioctl_grt_eint_irq_request, IOCTL_GRT_EINT_IRQ_REQUEST, pdev_eint_req_t,
                    zx_handle_t);

// ssize_t ioctl_grt_eint_set_type(int fd, const pdev_eint_set_type_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_eint_set_type, IOCTL_GRT_EINT_SET_TYPE, pdev_eint_set_type_t);

// ssize_t ioctl_grt_eint_mask(int fd, const pdev_eint_op_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_eint_mask, IOCTL_GRT_EINT_MASK, pdev_eint_op_t);

// ssize_t ioctl_grt_eint_unmask(int fd, const pdev_eint_op_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_eint_unmask, IOCTL_GRT_EINT_UNMASK, pdev_eint_op_t);

// ssize_t ioctl_grt_eint_ack(int fd, const pdev_eint_op_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_eint_ack, IOCTL_GRT_EINT_ACK, pdev_eint_op_t);

// ssize_t ioctl_grt_eint_get_max_eint_num(int fd, uint32_t* out);
IOCTL_WRAPPER_OUT(ioctl_grt_eint_get_max_eint_num, IOCTL_GRT_EINT_GET_MAX_EINT_NUM, uint32_t);

// ssize_t ioctl_grt_eint_vm_destroy(int fd, const uint16_t* vmid);
IOCTL_WRAPPER_IN(ioctl_grt_eint_vm_destroy, IOCTL_GRT_EINT_VM_DESTROY, uint16_t);

__END_CDECLS
