// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/mp.h>

#include <arch/ops.h>
#include <assert.h>
#include <dev/interrupt.h>
#include <err.h>
#include <kernel/event.h>
#include <platform.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

// map of cluster/cpu to cpu_id
uint arm64_cpu_map[SMP_CPU_MAX_CLUSTERS][SMP_CPU_MAX_CLUSTER_CPUS] = {{0}};

// cpu id to cluster and id within cluster map
uint arm64_cpu_cluster_ids[SMP_MAX_CPUS] = {0};
uint arm64_cpu_cpu_ids[SMP_MAX_CPUS] = {0};

// total number of detected cpus
uint arm_num_cpus = 1;

// per cpu structures, each cpu will point to theirs using the x18 register
arm64_percpu arm64_percpu_array[SMP_MAX_CPUS];

// initializes cpu_map and arm_num_cpus
void arch_init_cpu_map(uint cluster_count, const uint* cluster_cpus) {
    ASSERT(cluster_count <= SMP_CPU_MAX_CLUSTERS);

    // assign cpu_ids sequentially
    uint cpu_id = 0;
    for (uint cluster = 0; cluster < cluster_count; cluster++) {
        uint cpus = *cluster_cpus++;
        ASSERT(cpus <= SMP_CPU_MAX_CLUSTER_CPUS);
        for (uint cpu = 0; cpu < cpus; cpu++) {
            // given cluster:cpu, translate to global cpu id
            arm64_cpu_map[cluster][cpu] = cpu_id;

            // given global gpu_id, translate to cluster and cpu number within cluster
            arm64_cpu_cluster_ids[cpu_id] = cluster;
            arm64_cpu_cpu_ids[cpu_id] = cpu;

            // set the per cpu structure's cpu id
            arm64_percpu_array[cpu_id].cpu_num = cpu_id;

            cpu_id++;
        }
    }
    arm_num_cpus = cpu_id;
    smp_mb();
}

// do the 'slow' lookup by mpidr to cpu number
static uint arch_curr_cpu_num_slow() {
    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);
    uint cluster = (mpidr & MPIDR_AFF1_MASK) >> MPIDR_AFF1_SHIFT;
    uint cpu = (mpidr & MPIDR_AFF0_MASK) >> MPIDR_AFF0_SHIFT;

    return arm64_cpu_map[cluster][cpu];
}

cpu_num_t arch_mpid_to_cpu_num(uint cluster, uint cpu) {
    return arm64_cpu_map[cluster][cpu];
}

void arch_prepare_current_cpu_idle_state(bool idle) {
    // no-op
}

zx_status_t arch_mp_reschedule(cpu_mask_t mask) {
    return arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_RESCHEDULE);
}

zx_status_t arch_mp_send_ipi(mp_ipi_target_t target, cpu_mask_t mask, mp_ipi_t ipi) {
    LTRACEF("target %d mask %#x, ipi %d\n", target, mask, ipi);

    // translate the high level target + mask mechanism into just a mask
    switch (target) {
    case MP_IPI_TARGET_ALL:
        mask = (1ul << SMP_MAX_CPUS) - 1;
        break;
    case MP_IPI_TARGET_ALL_BUT_LOCAL:
        mask = (1ul << SMP_MAX_CPUS) - 1;
        mask &= ~cpu_num_to_mask(arch_curr_cpu_num());
        break;
    case MP_IPI_TARGET_MASK:;
    }

    return interrupt_send_ipi(mask, ipi);
}

void arm64_init_percpu_early(void) {
    // slow lookup the current cpu id and setup the percpu structure
    uint cpu = arch_curr_cpu_num_slow();

    arm64_write_percpu_ptr(&arm64_percpu_array[cpu]);
}

void arch_mp_init_percpu(void) {
    interrupt_init_percpu();
}

void arch_flush_state_and_halt(event_t* flush_done) {
    DEBUG_ASSERT(arch_ints_disabled());
    event_signal(flush_done, false);
    platform_halt_cpu();
    panic("control should never reach here\n");
}

zx_status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
    if (cpu_id == 0 || cpu_id >= arm_num_cpus) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t arch_mp_cpu_unplug(uint cpu_id) {
    // we do not allow unplugging the bootstrap processor
    if (cpu_id == 0 || cpu_id >= arm_num_cpus) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}
