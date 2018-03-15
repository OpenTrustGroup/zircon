// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform.h>

#include <arch/hypervisor.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <platform/timer.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static constexpr uint32_t kSpsrDaif = 0b1111 << 6;
static constexpr uint32_t kSpsrEl1h = 0b0101;
static constexpr uint32_t kSpsrNzcv = 0b1111 << 28;

static uint64_t vmpidr_of(uint8_t vpid, uint64_t mpidr) {
    return (vpid - 1) | (mpidr & 0xffffff00fe000000);
}

static bool gich_maybe_interrupt(GuestState* guest_state, GichState* gich_state) {
    uint64_t elrs = gic_read_gich_elrs();
    if (elrs == 0) {
        // All list registers are in use, therefore return and indicate that we
        // should raise an IRQ.
        return true;
    }
    zx_status_t status;
    uint32_t pending = 0;
    uint32_t vector = kTimerVector;
    if (gich_state->interrupt_tracker.TryPop(kTimerVector)) {
        // We give timer interrupts precedence over all others. If we find a
        // timer interrupt is pending, process it first.
        goto has_timer;
    }
    while (elrs != 0) {
        status = gich_state->interrupt_tracker.Pop(&vector);
        if (status != ZX_OK) {
            // There are no more pending interrupts.
            break;
        }
    has_timer:
        pending++;
        if (gich_state->active_interrupts.GetOne(vector)) {
            // Skip an interrupt if it was already active.
            continue;
        }
        uint32_t lr_index = __builtin_ctzl(elrs);
        uint32_t lr = GICH_LR_PENDING | (vector & GICH_LR_VIRTUAL_ID_MASK);
        gic_write_gich_lr(lr_index, lr);
        elrs &= ~(1u << lr_index);
    }
    // If there are pending interrupts, indicate that we should raise an IRQ.
    return pending > 0;
}

static void gich_active_interrupts(InterruptBitmap* active_interrupts) {
    active_interrupts->ClearAll();
    uint32_t lr_limit = __builtin_ctzl(gic_read_gich_elrs());
    for (uint32_t i = 0; i < lr_limit; i++) {
        uint32_t vector = gic_read_gich_lr(i) & GICH_LR_VIRTUAL_ID_MASK;
        active_interrupts->SetOne(vector);
    }
}

AutoGich::AutoGich(GichState* gich_state)
    : gich_state_(gich_state) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();

    // Load
    gic_write_gich_vmcr(gich_state_->vmcr);
    gic_write_gich_elrs(gich_state_->elrs);
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        gic_write_gich_lr(i, gich_state->lr[i]);
    }
}

AutoGich::~AutoGich() {
    DEBUG_ASSERT(arch_ints_disabled());

    // Save
    gich_state_->vmcr = gic_read_gich_vmcr();
    gich_state_->elrs = gic_read_gich_elrs();
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        gich_state_->lr[i] = gic_read_gich_lr(i);
    }
    arch_enable_ints();
}

zx_status_t El2StatePtr::Alloc() {
    zx_status_t status = page_.Alloc(0);
    if (status != ZX_OK) {
        return status;
    }
    state_ = page_.VirtualAddress<El2State>();
    return ZX_OK;
}

// static
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, fbl::unique_ptr<Vcpu>* out) {
    hypervisor::GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (entry >= gpas->size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t vpid;
    zx_status_t status = guest->AllocVpid(&vpid);
    if (status != ZX_OK) {
        return status;
    }
    auto auto_call = fbl::MakeAutoCall([guest, vpid]() { guest->FreeVpid(vpid); });

    // For efficiency, we pin the thread to the CPU.
    thread_t* thread = hypervisor::pin_thread(vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto_call.cancel();

    timer_init(&vcpu->gich_state_.timer);
    status = vcpu->gich_state_.interrupt_tracker.Init();
    if (status != ZX_OK) {
        return status;
    }

    status = vcpu->el2_state_.Alloc();
    if (status != ZX_OK) {
        return status;
    }

    gic_write_gich_hcr(GICH_HCR_EN);
    vcpu->gich_state_.active_interrupts.Reset(kNumInterrupts);
    vcpu->gich_state_.num_lrs = (gic_read_gich_vtr() & GICH_VTR_LIST_REGS_MASK) + 1;
    vcpu->gich_state_.elrs = (1u << vcpu->gich_state_.num_lrs) - 1;
    vcpu->el2_state_->guest_state.system_state.elr_el2 = entry;
    vcpu->el2_state_->guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);
    vcpu->el2_state_->guest_state.system_state.vmpidr_el2 = vmpidr_of(vpid, mpidr);
    vcpu->el2_state_->host_state.system_state.vmpidr_el2 = mpidr;
    vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO | HCR_EL2_FB |
                 HCR_EL2_BSU_IS | HCR_EL2_DC | HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC |
                 HCR_EL2_TVM | HCR_EL2_RW;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint8_t vpid, const thread_t* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), running_(false) {
    (void)thread_;
}

Vcpu::~Vcpu() {
    __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
    DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    const ArchVmAspace& aspace = guest_->AddressSpace()->aspace()->arch_aspace();
    zx_paddr_t vttbr = arm64_vttbr(aspace.arch_asid(), aspace.arch_table_phys());
    GuestState* guest_state = &el2_state_->guest_state;
    zx_status_t status;
    do {
        {
            AutoGich auto_gich(&gich_state_);
            uint64_t curr_hcr = hcr_;
            if (gich_maybe_interrupt(guest_state, &gich_state_)) {
                curr_hcr |= HCR_EL2_VI;
            }
            running_.store(true);
            status = arm64_el2_resume(vttbr, el2_state_.PhysicalAddress(), curr_hcr);
            running_.store(false);
            gich_active_interrupts(&gich_state_.active_interrupts);
        }
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt. If it was due to the thread
            // being killed, then we should exit with an error, otherwise return
            // to the guest.
            status = get_current_thread()->signals & THREAD_SIGNAL_KILL ? ZX_ERR_CANCELED : ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&hcr_, guest_state, &gich_state_, guest_->AddressSpace(),
                                    guest_->Traps(), packet);
        } else {
            dprintf(INFO, "VCPU resume failed: %d\n", status);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
    bool signaled = false;
    zx_status_t status = gich_state_.interrupt_tracker.Interrupt(vector, &signaled);
    if (status != ZX_OK) {
        return status;
    } else if (!signaled && running_.load()) {
        mp_reschedule(cpu_num_to_mask(hypervisor::cpu_of(vpid_)), MP_RESCHEDULE_FLAG_USE_IPI);
    }
    return ZX_OK;
}

zx_status_t Vcpu::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
        return ZX_ERR_BAD_STATE;
    } else if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto state = static_cast<zx_vcpu_state_t*>(buffer);
    memcpy(state->x, el2_state_->guest_state.x, sizeof(uint64_t) * GS_NUM_REGS);
    state->sp = el2_state_->guest_state.system_state.sp_el1;
    state->cpsr = el2_state_->guest_state.system_state.spsr_el2 & kSpsrNzcv;
    return ZX_OK;
}

zx_status_t Vcpu::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
        return ZX_ERR_BAD_STATE;
    } else if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto state = static_cast<const zx_vcpu_state_t*>(buffer);
    memcpy(el2_state_->guest_state.x, state->x, sizeof(uint64_t) * GS_NUM_REGS);
    el2_state_->guest_state.system_state.sp_el1 = state->sp;
    el2_state_->guest_state.system_state.spsr_el2 |= state->cpsr & kSpsrNzcv;
    return ZX_OK;
}
