// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <vm/pmm.h>
#include <zircon/types.h>

// GIC HW interface
struct arm_gic_hw_interface_ops {
    uint32_t (*read_gich_hcr)();
    void (*write_gich_hcr)(uint32_t val);
    uint32_t (*read_gich_vtr)();
    uint32_t (*default_gich_vmcr)();
    uint32_t (*read_gich_vmcr)();
    void (*write_gich_vmcr)(uint32_t val);
    uint32_t (*read_gich_misr)();
    uint64_t (*read_gich_elrsr)();
    uint32_t (*read_gich_apr)();
    void (*write_gich_apr)(uint32_t val);
    uint64_t (*read_gich_lr)(uint32_t idx);
    void (*write_gich_lr)(uint32_t idx, uint64_t val);
    zx_status_t (*get_gicv)(paddr_t* gicv_paddr);
    uint64_t (*get_lr_from_vector)(uint32_t);
    uint32_t (*get_vector_from_lr)(uint64_t);
    uint32_t (*get_num_lrs)();
};

// Returns the GICH_HCR value.
uint32_t gic_read_gich_hcr();

// Writes to the GICH_HCR register.
void gic_write_gich_hcr(uint32_t val);

// Returns the GICH_VTR value.
uint32_t gic_read_gich_vtr();

// Returns the default GICH_VMCR value. Used to initialize GICH_VMCR.
uint32_t gic_default_gich_vmcr();

// Returns the GICH_VMCR value.
uint32_t gic_read_gich_vmcr();

// Writes to the GICH_VMCR register.
void gic_write_gich_vmcr(uint32_t val);

// Returns the GICH_MISR value.
uint32_t gic_read_gich_misr();

// Returns the GICH_ELRS value.
uint64_t gic_read_gich_elrsr();

// Returns the GICH_LRn value.
uint64_t gic_read_gich_lr(uint32_t idx);

// Writes to the GICH_LR register.
void gic_write_gich_lr(uint32_t idx, uint64_t val);

// Get the GICV physical address.
zx_status_t gic_get_gicv(paddr_t* gicv_paddr);

uint64_t gic_get_lr_from_vector(uint32_t vector);

uint32_t gic_get_vector_from_lr(uint64_t lr);

// Registers the ops of the GIC driver initialized with HW interface layer.
void arm_gic_hw_interface_register(const struct arm_gic_hw_interface_ops* ops);

bool arm_gic_is_registered();

uint32_t gic_get_num_lrs();

// Returns the GICH_APR value.
uint32_t gic_read_gich_apr();

// Writes to the GICH_APR register.
void gic_write_gich_apr(uint32_t val);
