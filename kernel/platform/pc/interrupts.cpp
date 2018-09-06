// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "platform_p.h"
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>
#include <assert.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <lib/pow2_range_allocator.h>
#include <lk/init.h>
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/pic.h>
#include <pow2.h>
#include <reg.h>
#include <sys/types.h>
#include <zircon/types.h>

#include "platform_p.h"

#define MAX_IRQ_BLOCK_SIZE MAX_MSI_IRQS

#include <trace.h>

struct int_handler_struct {
    SpinLock lock;
    int_handler handler;
    void* arg;
};

static SpinLock lock;
static struct int_handler_struct int_handler_table[X86_INT_COUNT];
static p2ra_state_t x86_irq_vector_allocator;

static void platform_init_apic(uint level) {
    pic_map(PIC1_BASE, PIC2_BASE);
    pic_disable();

    // Enumerate the IO APICs
    uint32_t num_io_apics;
    zx_status_t status = platform_enumerate_io_apics(NULL, 0, &num_io_apics);
    // TODO: If we want to support x86 without IO APICs, we should do something
    // better here.
    ASSERT(status == ZX_OK);
    io_apic_descriptor* io_apics =
        static_cast<io_apic_descriptor*>(calloc(num_io_apics, sizeof(*io_apics)));
    ASSERT(io_apics != NULL);
    uint32_t num_found = 0;
    status = platform_enumerate_io_apics(io_apics, num_io_apics, &num_found);
    ASSERT(status == ZX_OK);
    ASSERT(num_io_apics == num_found);

    // Enumerate the IO APICs
    uint32_t num_isos;
    status = platform_enumerate_interrupt_source_overrides(NULL, 0, &num_isos);
    ASSERT(status == ZX_OK);
    io_apic_isa_override* isos = NULL;
    if (num_isos > 0) {
        isos = static_cast<io_apic_isa_override*>(calloc(num_isos, sizeof(*isos)));
        ASSERT(isos != NULL);
        status = platform_enumerate_interrupt_source_overrides(
            isos,
            num_isos,
            &num_found);
        ASSERT(status == ZX_OK);
        ASSERT(num_isos == num_found);
    }

    apic_vm_init();
    apic_local_init();
    apic_io_init(io_apics, num_io_apics, isos, num_isos);

    free(io_apics);
    free(isos);

    ASSERT(arch_ints_disabled());

    // Initialize the delivery modes/targets for the ISA interrupts
    uint8_t bsp_apic_id = apic_bsp_id();
    for (uint8_t irq = 0; irq < 8; ++irq) {
        // Explicitly skip mapping the PIC2 interrupt, since it is actually
        // just used internally on the PICs for daisy chaining.  QEMU remaps
        // ISA IRQ 0 to global IRQ 2, but does not remap ISA IRQ 2 off of
        // global IRQ 2, so skipping this mapping also prevents a collision
        // with the PIT IRQ.
        if (irq != ISA_IRQ_PIC2) {
            apic_io_configure_isa_irq(
                irq,
                DELIVERY_MODE_FIXED,
                IO_APIC_IRQ_MASK,
                DST_MODE_PHYSICAL,
                bsp_apic_id,
                0);
        }
        apic_io_configure_isa_irq(
            static_cast<uint8_t>(irq + 8),
            DELIVERY_MODE_FIXED,
            IO_APIC_IRQ_MASK,
            DST_MODE_PHYSICAL,
            bsp_apic_id,
            0);
    }

    // Initialize the x86 IRQ vector allocator and add the range of vectors to manage.
    status = p2ra_init(&x86_irq_vector_allocator, MAX_IRQ_BLOCK_SIZE);
    ASSERT(status == ZX_OK);

    status = p2ra_add_range(&x86_irq_vector_allocator,
                            X86_INT_PLATFORM_BASE,
                            X86_INT_PLATFORM_MAX - X86_INT_PLATFORM_BASE + 1);
    ASSERT(status == ZX_OK);
}
LK_INIT_HOOK(apic, &platform_init_apic, LK_INIT_LEVEL_VM + 2);

zx_status_t mask_interrupt(unsigned int vector) {
    AutoSpinLock guard(&lock);
    apic_io_mask_irq(vector, IO_APIC_IRQ_MASK);
    return ZX_OK;
}

zx_status_t unmask_interrupt(unsigned int vector) {
    AutoSpinLock guard(&lock);
    apic_io_mask_irq(vector, IO_APIC_IRQ_UNMASK);
    return ZX_OK;
}

zx_status_t configure_interrupt(unsigned int vector,
                                enum interrupt_trigger_mode tm,
                                enum interrupt_polarity pol) {
    AutoSpinLock guard(&lock);
    apic_io_configure_irq(
        vector,
        tm,
        pol,
        DELIVERY_MODE_FIXED,
        IO_APIC_IRQ_MASK,
        DST_MODE_PHYSICAL,
        apic_bsp_id(),
        0);
    return ZX_OK;
}

zx_status_t get_interrupt_config(unsigned int vector,
                                 enum interrupt_trigger_mode* tm,
                                 enum interrupt_polarity* pol) {
    AutoSpinLock guard(&lock);
    return apic_io_fetch_irq_config(vector, tm, pol);
}

void platform_irq(x86_iframe_t* frame) {
    // get the current vector
    uint64_t x86_vector = frame->vector;
    DEBUG_ASSERT(x86_vector >= X86_INT_PLATFORM_BASE &&
                 x86_vector <= X86_INT_PLATFORM_MAX);

    // deliver the interrupt
    struct int_handler_struct* handler = &int_handler_table[x86_vector];

    {
        AutoSpinLockNoIrqSave guard(&handler->lock);
        if (handler->handler)
            handler->handler(handler->arg);
    }

    apic_issue_eoi();
}

zx_status_t register_int_handler(unsigned int vector, int_handler handler, void* arg) {
    if (!is_valid_interrupt(vector, 0)) {
        return ZX_ERR_INVALID_ARGS;
    }

    AutoSpinLock guard(&lock);
    zx_status_t result = ZX_OK;

    /* Fetch the x86 vector currently configured for this global irq.  Force
     * it's value to zero if it is currently invalid */
    uint8_t x86_vector = apic_io_fetch_irq_vector(vector);
    if ((x86_vector < X86_INT_PLATFORM_BASE) ||
        (x86_vector > X86_INT_PLATFORM_MAX))
        x86_vector = 0;

    if (x86_vector && !handler) {
        /* If the x86 vector is valid, and we are unregistering the handler,
         * return the x86 vector to the pool. */
        p2ra_free_range(&x86_irq_vector_allocator, x86_vector, 1);
        x86_vector = 0;
    } else if (!x86_vector && handler) {
        /* If the x86 vector is invalid, and we are registering a handler,
         * attempt to get a new x86 vector from the pool. */
        uint range_start;

        /* Right now, there is not much we can do if the allocation fails.  In
         * debug builds, we ASSERT that everything went well.  In release
         * builds, we log a message and then silently ignore the request to
         * register a new handler. */
        result = p2ra_allocate_range(&x86_irq_vector_allocator, 1, &range_start);
        DEBUG_ASSERT(result == ZX_OK);

        if (result != ZX_OK) {
            TRACEF("Failed to allocate x86 IRQ vector for global IRQ (%u) when "
                   "registering new handler (%p, %p)\n",
                   vector, handler, arg);
            return result;
        }

        DEBUG_ASSERT((range_start >= X86_INT_PLATFORM_BASE) &&
                     (range_start <= X86_INT_PLATFORM_MAX));
        x86_vector = (uint8_t)range_start;
    }

    // Update the handler table and register the x86 vector with the io_apic.
    DEBUG_ASSERT(!!x86_vector == !!handler);

    {
        // No need to irq_save; we already did that when we grabbed the outer lock.
        AutoSpinLockNoIrqSave handler_guard(&int_handler_table[x86_vector].lock);

        if (handler && int_handler_table[x86_vector].handler) {
            p2ra_free_range(&x86_irq_vector_allocator, x86_vector, 1);
            return ZX_ERR_ALREADY_BOUND;
        }

        int_handler_table[x86_vector].handler = handler;
        int_handler_table[x86_vector].arg = handler ? arg : NULL;
    }

    apic_io_configure_irq_vector(vector, x86_vector);

    return ZX_OK;
}

uint32_t interrupt_get_base_vector(void) {
    // Intel Software Developer's Manual v3 chapter 6.2
    // 0-31 are reserved for architecture defined interrupts & exceptions
    return 32;
}

uint32_t interrupt_get_max_vector(void) {
    // x64 APIC supports 256 total vectors
    return 255;
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return apic_io_is_valid_irq(vector);
}

unsigned int remap_interrupt(unsigned int vector) {
    if (vector > NUM_ISA_IRQS) {
        return vector;
    }
    return apic_io_isa_to_global(static_cast<uint8_t>(vector));
}

void shutdown_interrupts(void) {
    pic_disable();
}

void shutdown_interrupts_curr_cpu(void) {
    // TODO(maniscalco): Walk interrupt redirection entries and make sure nothing targets this CPU.
}

// Intel 64 socs support the IOAPIC and Local APIC which support MSI by default.
// See 10.1, 10.4, and 10.11 of Intel® 64 and IA-32 Architectures Software Developer’s
// Manual 3A
bool msi_is_supported(void) {
    return true;
}

zx_status_t msi_alloc_block(uint requested_irqs,
                                bool can_target_64bit,
                                bool is_msix,
                                msi_block_t* out_block) {
    if (!out_block) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (out_block->allocated) {
        return ZX_ERR_BAD_STATE;
    }

    if (!requested_irqs || (requested_irqs > MAX_MSI_IRQS)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t res;
    uint alloc_start;
    uint alloc_size = 1u << log2_uint_ceil(requested_irqs);

    res = p2ra_allocate_range(&x86_irq_vector_allocator, alloc_size, &alloc_start);
    if (res == ZX_OK) {
        // Compute the target address.
        // See section 10.11.1 of the Intel 64 and IA-32 Architectures Software
        // Developer's Manual Volume 3A.
        //
        // TODO(johngro) : don't just bind this block to the Local APIC of the
        // processor which is active when calling msi_alloc_block.  Instead,
        // there should either be a system policy (like, always send to any
        // processor, or just processor 0, or something), or the decision of
        // which CPUs to bind to should be left to the caller.
        uint32_t tgt_addr = 0xFEE00000;              // base addr
        tgt_addr |= ((uint32_t)apic_bsp_id()) << 12; // Dest ID == the BSP APIC ID
        tgt_addr |= 0x08;                            // Redir hint == 1
        tgt_addr &= ~0x04;                           // Dest Mode == Physical

        // Compute the target data.
        // See section 10.11.2 of the Intel 64 and IA-32 Architectures Software
        // Developer's Manual Volume 3A.
        //
        // delivery mode == 0 (fixed)
        // trigger mode  == 0 (edge)
        // vector == start of block range
        DEBUG_ASSERT(!(alloc_start & ~0xFF));
        DEBUG_ASSERT(!(alloc_start & (alloc_size - 1)));
        uint32_t tgt_data = alloc_start;

        /* Success!  Fill out the bookkeeping and we are done */
        out_block->platform_ctx = NULL;
        out_block->base_irq_id = alloc_start;
        out_block->num_irq = alloc_size;
        out_block->tgt_addr = tgt_addr;
        out_block->tgt_data = tgt_data;
        out_block->allocated = true;
    }

    return res;
}

void msi_free_block(msi_block_t* block) {
    DEBUG_ASSERT(block);
    DEBUG_ASSERT(block->allocated);
    p2ra_free_range(&x86_irq_vector_allocator, block->base_irq_id, block->num_irq);
    memset(block, 0, sizeof(*block));
}

void msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler, void* ctx) {
    DEBUG_ASSERT(block && block->allocated);
    DEBUG_ASSERT(msi_id < block->num_irq);

    uint x86_vector = msi_id + block->base_irq_id;
    DEBUG_ASSERT((x86_vector >= X86_INT_PLATFORM_BASE) &&
                 (x86_vector <= X86_INT_PLATFORM_MAX));

    AutoSpinLock guard(&int_handler_table[x86_vector].lock);
    int_handler_table[x86_vector].handler = handler;
    int_handler_table[x86_vector].arg = handler ? ctx : NULL;
}
