// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Boot modes zx_system_get_bootmode reports.
//
// ZX_BOOTMODE_PHYP: this kernel runs as an ordinary OS.
// ZX_BOOTMODE_AHYP: this kernel is the hypervisor that runs the guests: it
//                   started at EL2.
// ZX_BOOTMODE_TEE:  this kernel is a TEE. NORA has none; the value is only
//                   reserved.
#define ZX_BOOTMODE_PHYP 0u
#define ZX_BOOTMODE_AHYP 1u
#define ZX_BOOTMODE_TEE  2u

#ifdef ZX_NEBULA_API
// Nebula's system call interface, Nebula's zircon's as of April 2018, for code
// written against it (garnet, and the Trusty-era code beside it), over this
// kernel's. The Fuchsia build defines ZX_NEBULA_API and includes this header
// in front of every such file (//build/config:nebula_api); Zircon's own code,
// which includes it for the constants above, never sees what follows. The C++
// libraries offer Nebula's interfaces next to their own where ZX_NEBULA_API
// is defined (lib/zx, lib/async).
//
// - Calls whose arguments come in another order map onto this kernel's.
// - Calls this kernel does not have return ZX_ERR_NOT_SUPPORTED, except where
//   having nothing to do is the right answer (a guest's TEE context: NORA has
//   no TEE).

#include <stdbool.h>

#include <stddef.h>
#include <stdint.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

// The C standard gives UINT8_MAX and UINT16_MAX the type their values take
// after integer promotion, int, as musl's <stdint.h> in Nebula's sysroot does;
// clang's own <stdint.h>, which this sysroot uses, makes them unsigned.
#undef UINT8_MAX
#define UINT8_MAX 255
#undef UINT16_MAX
#define UINT16_MAX 65535

// The structure that contains |ptr|, a pointer to its |member|. Spelled as
// garnet's <linux/types.h> spells it, so that the two definitions agree.
#define container_of(ptr, type, member) \
  ((type*)((char*)(ptr)-offsetof(type, member)))

__BEGIN_CDECLS

// An ARM SMMUv3 IOMMU and the events it reports. This kernel has no such
// IOMMU: zx_iommu_create rejects the type.
#define ZX_IOMMU_TYPE_ARM_SMMUV3 2

typedef struct zx_iommu_desc_smmuv3 {
    uint32_t id;
} zx_iommu_desc_smmuv3_t;

typedef struct zx_packet_iommu_event {
    uint32_t evt[8];
} zx_packet_iommu_event_t;

// zx_vmar_map, zx_vmar_allocate and zx_vmar_protect take their flags second
// in Nebula's interface, last in this kernel's.

static inline zx_status_t __nebula_vmar_map(zx_handle_t vmar, uint32_t flags, size_t vmar_offset,
                                         zx_handle_t vmo, uint64_t vmo_offset, size_t len,
                                         uintptr_t* mapped_addr) {
    return _zx_vmar_map(vmar, vmar_offset, vmo, vmo_offset, len, flags, mapped_addr);
}

static inline zx_status_t __nebula_vmar_allocate(zx_handle_t parent, uint32_t flags, size_t offset,
                                              size_t size, zx_handle_t* child_vmar,
                                              uintptr_t* child_addr) {
    return _zx_vmar_allocate(parent, offset, size, flags, child_vmar, child_addr);
}

static inline zx_status_t __nebula_vmar_protect(zx_handle_t vmar, uint32_t prot, uintptr_t addr,
                                             size_t len) {
    return _zx_vmar_protect(vmar, addr, len, prot);
}

#define zx_vmar_map(...) __nebula_vmar_map(__VA_ARGS__)
#define zx_vmar_allocate(...) __nebula_vmar_allocate(__VA_ARGS__)
#define zx_vmar_protect(...) __nebula_vmar_protect(__VA_ARGS__)

// Clocks Nebula's kernel adds: the RTC, which is UTC here, and the time zone, in
// minutes west of UTC, which this kernel does not keep (UTC, offset 0).
#define ZX_CLOCK_RTC         (0x100u)
#define ZX_CLOCK_TZ_MIN_WEST (0x101u)

static inline zx_time_t __nebula_clock_get(uint32_t clock_id) {
    if (clock_id == ZX_CLOCK_RTC)
        return _zx_clock_get(ZX_CLOCK_UTC);
    if (clock_id == ZX_CLOCK_TZ_MIN_WEST)
        return 0;
    return _zx_clock_get(clock_id);
}

static inline zx_status_t __nebula_clock_adjust(zx_handle_t handle, uint32_t clock_id,
                                             int64_t offset) {
    if (clock_id == ZX_CLOCK_RTC)
        return _zx_clock_adjust(handle, ZX_CLOCK_UTC, offset);
    if (clock_id == ZX_CLOCK_TZ_MIN_WEST)
        return ZX_ERR_NOT_SUPPORTED;
    return _zx_clock_adjust(handle, clock_id, offset);
}

#define zx_clock_get(...) __nebula_clock_get(__VA_ARGS__)
#define zx_clock_adjust(...) __nebula_clock_adjust(__VA_ARGS__)

// Job policies Nebula's kernel adds for Trusty applications. NORA has no
// Trusty, so there is nothing for them to allow or deny: zx_job_set_policy
// leaves them out of what it passes on.
#define ZX_POL_TRUSTY          (0x10000u)
#define ZX_POL_CALL_TRUSTY_API (0x10001u)

static inline zx_status_t __nebula_job_set_policy(zx_handle_t job, uint32_t options,
                                               uint32_t topic, const void* policy,
                                               uint32_t count) {
    if (topic != ZX_JOB_POL_BASIC || count > ZX_POL_MAX + 2u)
        return _zx_job_set_policy(job, options, topic, policy, count);
    const zx_policy_basic_t* in = (const zx_policy_basic_t*)policy;
    zx_policy_basic_t kept[ZX_POL_MAX + 2u];
    uint32_t kept_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (in[i].condition != ZX_POL_TRUSTY && in[i].condition != ZX_POL_CALL_TRUSTY_API)
            kept[kept_count++] = in[i];
    }
    if (kept_count == 0)
        return ZX_OK;
    return _zx_job_set_policy(job, options, topic, kept, kept_count);
}

#define zx_job_set_policy(...) __nebula_job_set_policy(__VA_ARGS__)

// Nebula's zx_smc_call takes a zx_smc_parameters_t and fills a zx_smc_result_t;
// this kernel's takes four arguments and returns x0 only. Its users are the
// TEE and a board's SiP services, which NORA does not have.
static inline zx_status_t __nebula_smc_call(zx_handle_t handle, const zx_smc_parameters_t* parameters,
                                         zx_smc_result_t* result) {
    return ZX_ERR_NOT_SUPPORTED;
}

#define zx_smc_call(...) __nebula_smc_call(__VA_ARGS__)

// Guest vCPU packets Nebula's kernel adds; this kernel sends neither.
#define ZX_PKT_GUEST_VCPU_VHM_REQ 0x80
#define ZX_PKT_GUEST_VCPU_STOP    0x81

// The status a VMM's PCI device returns when the guest moved a BAR.
#define ZX_ERR_PCI_BAR_REALLOC (-100)

// Nebula's slot-based interrupt calls are zx_interrupt_slot_* here.
#define zx_interrupt_create(...) zx_interrupt_slot_create(__VA_ARGS__)
#define zx_interrupt_bind(...) zx_interrupt_slot_bind(__VA_ARGS__)
#define zx_interrupt_wait(...) zx_interrupt_slot_wait(__VA_ARGS__)

// The TEE that zx_system_get_tee_vendor_id reports: NORA has none.
#define ZX_TEE_VENDOR_NONE      0u
#define ZX_TEE_VENDOR_NEBULA    1u
#define ZX_TEE_VENDOR_TRUSTONIC 2u
#define ZX_TEE_VENDOR_MASK      0xffu

static inline uint32_t zx_system_get_tee_vendor_id(void) {
    return ZX_TEE_VENDOR_NONE;
}

// A guest's TEE context and user VM ID: there is no TEE to tell, and the
// kernel keeps no user VM IDs, so there is nothing to do.

static inline zx_status_t zx_guest_tee_vm_create(zx_handle_t guest, int32_t user_vmid) {
    return ZX_OK;
}

static inline zx_status_t zx_guest_tee_vm_destroy(zx_handle_t guest, int32_t user_vmid) {
    return ZX_OK;
}

static inline zx_status_t zx_guest_set_user_vmid(zx_handle_t guest, int32_t user_vmid) {
    return ZX_OK;
}

// Calls this kernel does not have.

static inline zx_status_t zx_guest_remove_trap(zx_handle_t guest, uint32_t kind, zx_vaddr_t addr) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_guest_set_cpu_affinity(zx_handle_t guest, const uint32_t* pcpus,
                                                    uint32_t count) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_guest_set_cvm_lock_irq(zx_handle_t guest, uint32_t irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_guest_set_cvm_lock_vmo(zx_handle_t guest, zx_handle_t vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_guest_set_smc_irq(zx_handle_t guest, uint32_t irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_guest_set_wakeup_irqs(zx_handle_t guest, const uint32_t* irqs,
                                                   uint32_t count) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_vcpu_stop(zx_handle_t vcpu) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_vcpu_inject_exception(zx_handle_t vcpu, uint64_t addr) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_vcpu_dump_state(zx_handle_t vcpu, char* buffer, size_t size) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_get_pmstate_form_vmid(zx_handle_t resource, int32_t vmid,
                                                   int32_t* pm_state) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_power_on_cpu(uint32_t cpu) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_poweron_phy_cpu(uint32_t cpu) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_vmo_create_physical_paged(zx_handle_t resource,
                                                       const zx_paddr_t* pages, size_t count,
                                                       zx_handle_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_iommu_init_dev(zx_handle_t iommu) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_iommu_set_guest(zx_handle_t iommu, zx_handle_t guest) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_iommu_hwpt_invalidate(zx_handle_t iommu, void* cmds, size_t cmd_len,
                                                   uint32_t* ncmds) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_set_cache_protection(uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_poison_cache(void) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_suspend_notifier_init(zx_handle_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

// MediaTek board services: the SMC message channel and the GPU scheduler.

static inline zx_status_t zx_smc_communication_init(void) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_smc_communication_message_read(void* buffer, size_t size) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_smc_communication_force_reboot(void) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_smc_communication_secure_reboot(void) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_smc_communication_vm_poweroff(uint32_t src_vmid, uint32_t tgt_vmid) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_create(zx_handle_t resource, const uint32_t* gpu_irqs,
                                                  size_t gpu_irq_count,
                                                  const uint32_t* notify_irqs,
                                                  size_t notify_irq_count, const void* regions,
                                                  size_t region_count, zx_handle_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_add_vcpu(zx_handle_t scheduler, int32_t vmid,
                                                    zx_handle_t vcpu, uint64_t affinity) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_destroy_vm(zx_handle_t scheduler, int32_t vmid) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_set_aee_vmo(zx_handle_t scheduler, zx_handle_t vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_set_gpueb_shmem_vmo(zx_handle_t scheduler,
                                                               zx_handle_t vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_set_mode(zx_handle_t scheduler, uint32_t mode) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_set_timeslice(zx_handle_t scheduler, int32_t vmid,
                                                         const void* periods, size_t count) {
    return ZX_ERR_NOT_SUPPORTED;
}

static inline zx_status_t zx_gpu_scheduler_set_vmo(zx_handle_t scheduler, int32_t vmid,
                                                   zx_handle_t vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

__END_CDECLS

#endif // ZX_NEBULA_API
