// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmexit_priv.h"

#include <bits.h>
#include <platform.h>
#include <trace.h>

#include <arch/arm64/el2_state.h>
#include <arch/hypervisor.h>
#include <dev/psci.h>
#include <dev/timer/arm_generic.h>
#include <hypervisor/ktrace.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#define LOCAL_TRACE 0

#define SET_SYSREG(sysreg)                                                      \
    ({                                                                          \
        guest_state->system_state.sysreg = reg;                                 \
        LTRACEF("guest " #sysreg ": %#lx\n", guest_state->system_state.sysreg); \
        next_pc(guest_state);                                                   \
        ZX_OK;                                                                  \
    })

static constexpr size_t kPageTableLevelShift = 3;
static constexpr uint16_t kSmcPsci = 0;

enum TimerControl : uint64_t {
    ENABLE = 1u << 0,
    IMASK = 1u << 1,
};

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
    ec = static_cast<ExceptionClass>(BITS_SHIFT(esr, 31, 26));
    iss = BITS(esr, 24, 0);
}

WaitInstruction::WaitInstruction(uint32_t iss) {
    is_wfe = BIT(iss, 0);
}

SmcInstruction::SmcInstruction(uint32_t iss) {
    imm = static_cast<uint16_t>(BITS(iss, 15, 0));
}

SystemInstruction::SystemInstruction(uint32_t iss) {
    sysreg = static_cast<SystemRegister>(BITS(iss, 21, 10) >> 6 | BITS_SHIFT(iss, 4, 1));
    xt = static_cast<uint8_t>(BITS_SHIFT(iss, 9, 5));
    read = BIT(iss, 0);
}

SgiRegister::SgiRegister(uint64_t sgir) {
    aff3 = static_cast<uint8_t>(BITS_SHIFT(sgir, 55, 48));
    aff2 = static_cast<uint8_t>(BITS_SHIFT(sgir, 39, 32));
    aff1 = static_cast<uint8_t>(BITS_SHIFT(sgir, 23, 16));
    rs = static_cast<uint8_t>(BITS_SHIFT(sgir, 47, 44));
    target_list = static_cast<uint8_t>(BITS_SHIFT(sgir, 15, 0));
    int_id = static_cast<uint8_t>(BITS_SHIFT(sgir, 27, 24));
    all_but_local = BIT(sgir, 40);
}

DataAbort::DataAbort(uint32_t iss) {
    valid = BIT_SHIFT(iss, 24);
    access_size = static_cast<uint8_t>(1u << BITS_SHIFT(iss, 23, 22));
    sign_extend = BIT(iss, 21);
    xt = static_cast<uint8_t>(BITS_SHIFT(iss, 20, 16));
    read = !BIT(iss, 6);
}

static void next_pc(GuestState* guest_state) {
    guest_state->system_state.elr_el2 += 4;
}

static void deadline_callback(timer_t* timer, zx_time_t now, void* arg) {
    auto gich_state = static_cast<GichState*>(arg);
    __UNUSED zx_status_t status = gich_state->interrupt_tracker.Interrupt(kTimerVector, nullptr);
    DEBUG_ASSERT(status == ZX_OK);
}

static zx_status_t handle_wfi_wfe_instruction(uint32_t iss, GuestState* guest_state,
                                              GichState* gich_state) {
    next_pc(guest_state);
    const WaitInstruction wi(iss);
    if (wi.is_wfe) {
        ktrace_vcpu_exit(VCPU_WFE_INSTRUCTION, guest_state->system_state.elr_el2);
        thread_reschedule();
        return ZX_OK;
    }
    ktrace_vcpu_exit(VCPU_WFI_INSTRUCTION, guest_state->system_state.elr_el2);

    bool pending = gich_state->active_interrupts.GetOne(kTimerVector);
    bool enabled = guest_state->cntv_ctl_el0 & TimerControl::ENABLE;
    bool masked = guest_state->cntv_ctl_el0 & TimerControl::IMASK;
    if (pending || !enabled || masked) {
        thread_yield();
        return ZX_OK;
    }

    timer_cancel(&gich_state->timer);
    uint64_t cntpct_deadline = guest_state->cntv_cval_el0;
    zx_time_t deadline = cntpct_to_zx_time(cntpct_deadline);
    if (deadline <= current_time()) {
        return gich_state->interrupt_tracker.Track(kTimerVector);
    }

    timer_set_oneshot(&gich_state->timer, deadline, deadline_callback, gich_state);
    return gich_state->interrupt_tracker.Wait(nullptr);
}

static zx_status_t handle_smc_instruction(uint32_t iss, GuestState* guest_state,
                                          zx_port_packet_t* packet) {
    const SmcInstruction si(iss);
    if (si.imm != kSmcPsci)
        return ZX_ERR_NOT_SUPPORTED;

    next_pc(guest_state);
    switch (guest_state->x[0]) {
    case PSCI64_CPU_ON:
        memset(packet, 0, sizeof(*packet));
        packet->type = ZX_PKT_TYPE_GUEST_VCPU;
        packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_STARTUP;
        packet->guest_vcpu.startup.id = guest_state->x[1];
        packet->guest_vcpu.startup.entry = guest_state->x[2];
        guest_state->x[0] = PSCI_SUCCESS;
        return ZX_ERR_NEXT;
    default:
        guest_state->x[0] = PSCI_NOT_SUPPORTED;
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void clean_invalidate_cache(zx_paddr_t table, size_t index_shift) {
    // TODO(abdulla): Make this understand concatenated page tables.
    auto* pte = static_cast<pte_t*>(paddr_to_physmap(table));
    pte_t page = index_shift > MMU_GUEST_PAGE_SIZE_SHIFT ?
                 MMU_PTE_L012_DESCRIPTOR_BLOCK : MMU_PTE_L3_DESCRIPTOR_PAGE;
    for (size_t i = 0; i < PAGE_SIZE / sizeof(pte_t); i++) {
        pte_t desc = pte[i] & MMU_PTE_DESCRIPTOR_MASK;
        pte_t paddr = pte[i] & MMU_PTE_OUTPUT_ADDR_MASK;
        if (desc == page) {
            zx_vaddr_t vaddr = reinterpret_cast<zx_vaddr_t>(paddr_to_physmap(paddr));
            arch_clean_invalidate_cache_range(vaddr, 1lu << index_shift);
        } else if (desc != MMU_PTE_DESCRIPTOR_INVALID) {
            size_t adjust_shift = MMU_GUEST_PAGE_SIZE_SHIFT - kPageTableLevelShift;
            clean_invalidate_cache(paddr, index_shift - adjust_shift);
        }
    }
}

static zx_status_t handle_system_instruction(uint32_t iss, uint64_t* hcr, GuestState* guest_state,
                                             hypervisor::GuestPhysicalAddressSpace* gpas,
                                             zx_port_packet_t* packet) {
    const SystemInstruction si(iss);
    const uint64_t reg = guest_state->x[si.xt];

    switch (si.sysreg) {
    case SystemRegister::MAIR_EL1:
        return SET_SYSREG(mair_el1);
    case SystemRegister::SCTLR_EL1: {
        if (si.read) {
            return ZX_ERR_NOT_SUPPORTED;
        }

        // From ARM DDI 0487B.b, Section D10.2.89: If the value of HCR_EL2.{DC,
        // TGE} is not {0, 0} then in Non-secure state the PE behaves as if the
        // value of the SCTLR_EL1.M field is 0 for all purposes other than
        // returning the value of a direct read of the field.
        //
        // Therefore if SCTLR_EL1.M is set to 1, we need to set HCR_EL2.DC to 0
        // and invalidate the guest physical address space.
        uint32_t sctlr_el1 = reg & UINT32_MAX;
        if (sctlr_el1 & SCTLR_ELX_M) {
            *hcr &= ~HCR_EL2_DC;
            // Additionally, if the guest has also set SCTLR_EL1.C to 1, we no
            // longer need to trap writes to virtual memory control registers,
            // so we can set HCR_EL2.TVM to 0 to improve performance.
            if (sctlr_el1 & SCTLR_ELX_C) {
                *hcr &= ~HCR_EL2_TVM;
            }
            clean_invalidate_cache(gpas->arch_aspace()->arch_table_phys(), MMU_GUEST_TOP_SHIFT);
        }
        guest_state->system_state.sctlr_el1 = sctlr_el1;

        LTRACEF("guest sctlr_el1: %#x\n", sctlr_el1);
        LTRACEF("guest hcr_el2: %#lx\n", *hcr);
        next_pc(guest_state);
        return ZX_OK;
    }
    case SystemRegister::TCR_EL1:
        return SET_SYSREG(tcr_el1);
    case SystemRegister::TTBR0_EL1:
        return SET_SYSREG(ttbr0_el1);
    case SystemRegister::TTBR1_EL1:
        return SET_SYSREG(ttbr1_el1);
    case SystemRegister::OSLAR_EL1:
    case SystemRegister::OSLSR_EL1:
    case SystemRegister::OSDLR_EL1:
    case SystemRegister::DBGPRCR_EL1:
        next_pc(guest_state);
        // These registers are RAZ/WI. Their state is dictated by the host.
        if (si.read) {
            guest_state->x[si.xt] = 0;
        }
        return ZX_OK;
    case SystemRegister::ICC_SGI1R_EL1: {
        if (si.read) {
            // ICC_SGI1R_EL1 is write-only.
            return ZX_ERR_INVALID_ARGS;
        }
        SgiRegister sgi(reg);
        if (sgi.aff3 != 0 || sgi.aff2 != 0 || sgi.aff1 != 0 || sgi.rs != 0) {
            return ZX_ERR_NOT_SUPPORTED;
        }

        memset(packet, 0, sizeof(*packet));
        packet->type = ZX_PKT_TYPE_GUEST_VCPU;
        packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_INTERRUPT;
        if (sgi.all_but_local) {
            auto vpid = BITS(guest_state->system_state.vmpidr_el2, 8, 0);
            packet->guest_vcpu.interrupt.mask = ~(static_cast<uint64_t>(1) << vpid);
        } else {
            packet->guest_vcpu.interrupt.mask = sgi.target_list;
        }
        packet->guest_vcpu.interrupt.vector = sgi.int_id;
        next_pc(guest_state);
        return ZX_ERR_NEXT;
    }
    }

    dprintf(CRITICAL, "Unhandled system register %#x\n", static_cast<uint16_t>(si.sysreg));
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t handle_instruction_abort(GuestState* guest_state,
                                            hypervisor::GuestPhysicalAddressSpace* gpas) {
    const zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    zx_status_t status = gpas->PageFault(guest_paddr);
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Unhandled instruction abort %#lx\n", guest_paddr);
    }
    return status;
}

static zx_status_t handle_data_abort(uint32_t iss, GuestState* guest_state,
                                     hypervisor::GuestPhysicalAddressSpace* gpas,
                                     hypervisor::TrapMap* traps,
                                     zx_port_packet_t* packet) {
    zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    hypervisor::Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    switch (status) {
    case ZX_ERR_NOT_FOUND:
        status = gpas->PageFault(guest_paddr);
        if (status != ZX_OK) {
            dprintf(CRITICAL, "Unhandled data abort %#lx\n", guest_paddr);
        }
        return status;
    case ZX_OK:
        break;
    default:
        return status;
    }
    next_pc(guest_state);

    // Combine the lower bits of FAR_EL2 with HPFAR_EL2 to get the exact IPA.
    guest_paddr |= guest_state->far_el2 & (PAGE_SIZE - 1);
    LTRACEF("guest far_el2: %#lx\n", guest_state->far_el2);

    const DataAbort data_abort(iss);
    switch (trap->kind()) {
    case ZX_GUEST_TRAP_BELL:
        if (data_abort.read)
            return ZX_ERR_NOT_SUPPORTED;
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_BELL;
        packet->guest_bell.addr = guest_paddr;
        if (!trap->HasPort())
            return ZX_ERR_BAD_STATE;
        return trap->Queue(*packet, nullptr);
    case ZX_GUEST_TRAP_MEM:
        if (!data_abort.valid)
            return ZX_ERR_IO_DATA_INTEGRITY;
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_MEM;
        packet->guest_mem.addr = guest_paddr;
        packet->guest_mem.access_size = data_abort.access_size;
        packet->guest_mem.sign_extend = data_abort.sign_extend;
        packet->guest_mem.xt = data_abort.xt;
        packet->guest_mem.read = data_abort.read;
        if (!data_abort.read)
            packet->guest_mem.data = guest_state->x[data_abort.xt];
        return ZX_ERR_NEXT;
    default:
        return ZX_ERR_BAD_STATE;
    }
}

zx_status_t vmexit_handler(uint64_t* hcr, GuestState* guest_state, GichState* gich_state,
                           hypervisor::GuestPhysicalAddressSpace* gpas, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet) {
    LTRACEF("guest esr_el1: %#x\n", guest_state->system_state.esr_el1);
    LTRACEF("guest esr_el2: %#x\n", guest_state->esr_el2);
    LTRACEF("guest elr_el2: %#lx\n", guest_state->system_state.elr_el2);
    LTRACEF("guest spsr_el2: %#x\n", guest_state->system_state.spsr_el2);

    ExceptionSyndrome syndrome(guest_state->esr_el2);
    zx_status_t status;
    switch (syndrome.ec) {
    case ExceptionClass::WFI_WFE_INSTRUCTION:
        LTRACEF("handling wfi/wfe instruction, iss %#x\n", syndrome.iss);
        status = handle_wfi_wfe_instruction(syndrome.iss, guest_state, gich_state);
        break;
    case ExceptionClass::SMC_INSTRUCTION:
        LTRACEF("handling smc instruction, iss %#x func %#lx\n", syndrome.iss, guest_state->x[0]);
        ktrace_vcpu_exit(VCPU_SMC_INSTRUCTION, guest_state->system_state.elr_el2);
        status = handle_smc_instruction(syndrome.iss, guest_state, packet);
        break;
    case ExceptionClass::SYSTEM_INSTRUCTION:
        LTRACEF("handling system instruction\n");
        ktrace_vcpu_exit(VCPU_SYSTEM_INSTRUCTION, guest_state->system_state.elr_el2);
        status = handle_system_instruction(syndrome.iss, hcr, guest_state, gpas, packet);
        break;
    case ExceptionClass::INSTRUCTION_ABORT:
        LTRACEF("handling instruction abort at %#lx\n", guest_state->hpfar_el2);
        ktrace_vcpu_exit(VCPU_INSTRUCTION_ABORT, guest_state->system_state.elr_el2);
        status = handle_instruction_abort(guest_state, gpas);
        break;
    case ExceptionClass::DATA_ABORT:
        LTRACEF("handling data abort at %#lx\n", guest_state->hpfar_el2);
        ktrace_vcpu_exit(VCPU_DATA_ABORT, guest_state->system_state.elr_el2);
        status = handle_data_abort(syndrome.iss, guest_state, gpas, traps, packet);
        break;
    default:
        LTRACEF("unhandled exception syndrome, ec %#x iss %#x\n",
                static_cast<uint32_t>(syndrome.ec), syndrome.iss);
        ktrace_vcpu_exit(VCPU_UNKNOWN, guest_state->system_state.elr_el2);
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    }
    if (status != ZX_OK && status != ZX_ERR_NEXT && status != ZX_ERR_CANCELED) {
        dprintf(CRITICAL, "VM exit handler for %u (%s) to EL%u at %lx returned %d\n",
                static_cast<uint32_t>(syndrome.ec),
                exception_class_name(syndrome.ec),
                BITS_SHIFT(guest_state->system_state.spsr_el2, 3, 2),
                guest_state->system_state.elr_el2,
                status);
    }
    return status;
}
