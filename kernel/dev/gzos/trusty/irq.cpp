// Copyright 2018 Open Trust Group
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/smccc.h>
#include <dev/interrupt/arm_gic_common.h>
#include <err.h>
#include <kernel/event.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <pdev/interrupt.h>
#include <trace.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <zircon/syscalls/smc_defs.h>

#define LOCAL_TRACE 0

class TrustyIrq;
using TrustyIrqList = fbl::SinglyLinkedList<fbl::unique_ptr<TrustyIrq>>;

struct PendingIrqListTraits;
using PendingIrqList = fbl::SinglyLinkedList<TrustyIrq*, PendingIrqListTraits>;

struct TrustyIrqPerCpuState {
    PendingIrqList pending_list;
    event_t event;
};

struct TrustyIrqState {
    TrustyIrqList irq_list;
    PendingIrqList pending_list;
    spin_lock_t pending_list_lock;
    TrustyIrqPerCpuState percpu[SMP_MAX_CPUS];
};

static TrustyIrqState irq_state;

class TrustyIrq : public fbl::SinglyLinkedListable<fbl::unique_ptr<TrustyIrq>> {
public:
    static zx_status_t Create(uint32_t vector, fbl::unique_ptr<TrustyIrq>* out_irq) {
        fbl::AllocChecker ac;
        auto irq = fbl::make_unique_checked<TrustyIrq>(&ac, vector);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zx_status_t status = irq->RegisterInterruptHandler();
        if (status != ZX_OK) {
            TRACEF("failed to register IRQ handler, status=%d\n", status);
            return status;
        }

        *out_irq = fbl::move(irq);
        return ZX_OK;
    }

    explicit TrustyIrq(uint32_t vector)
        : vector_(vector) {
        percpu_ = (vector_ < GIC_BASE_SPI);
    }

    ~TrustyIrq() {
        UnRegisterInterruptHandler();
    }

    void MaskInterrupt() {
        mask_interrupt(vector_);
    }
    void UnmaskInterrupt() {
        unmask_interrupt(vector_);
    }

    using NodeState = fbl::SinglyLinkedListNodeState<TrustyIrq*>;
    NodeState pending_list_node;

private:
    zx_status_t RegisterInterruptHandler() {
        if (!percpu_) {
            zx_status_t status;
            status = configure_interrupt(vector_, IRQ_TRIGGER_MODE_LEVEL,
                                         IRQ_POLARITY_ACTIVE_HIGH);
            if (status != ZX_OK) {
                printf("failed to configure_interrupt, status=%d\n", status);
                return status;
            }
        }

        return register_int_handler(vector_, IrqHandler, this);
    }

    void UnRegisterInterruptHandler() {
        if (!percpu_) {
            configure_interrupt(vector_, IRQ_TRIGGER_MODE_EDGE,
                                IRQ_POLARITY_ACTIVE_HIGH);
        }
        register_int_handler(vector_, nullptr, nullptr);
    }

    static void IrqHandler(void* args) {
        auto* thiz = reinterpret_cast<TrustyIrq*>(args);
        auto cpu_num = arch_curr_cpu_num();
        auto& percpu_state = irq_state.percpu[cpu_num];

        LTRACEF("vector=%u, cpu_num=%u\n", thiz->vector_, cpu_num);
        thiz->MaskInterrupt();

        arch_disable_ints();
        if (thiz->percpu_) {
            percpu_state.pending_list.push_front(thiz);
        } else {
            spin_lock(&irq_state.pending_list_lock);
            irq_state.pending_list.push_front(thiz);
            spin_unlock(&irq_state.pending_list_lock);
        }
        arch_enable_ints();

        event_signal(&percpu_state.event, true);
    }

    uint32_t vector_;
    bool percpu_;
};

struct PendingIrqListTraits {
    static TrustyIrq::NodeState& node_state(TrustyIrq& ref) {
        return ref.pending_list_node;
    }
};

static int trusty_get_next_irq(uint32_t min_irq, bool percpu) {
    return (int)arm_smccc_smc(SMC_FC_GET_NEXT_IRQ, min_irq, percpu, 0, 0, 0, 0, 0).x0;
}

static zx_status_t trusty_irq_init(bool percpu) {
    int vector = trusty_get_next_irq(0, percpu);

    while (vector > 0) {
        LTRACEF("vector=%d\n", vector);

        fbl::unique_ptr<TrustyIrq> irq;
        zx_status_t status = TrustyIrq::Create(vector, &irq);
        if (status != ZX_OK) {
            return status;
        }

        irq->UnmaskInterrupt();

        irq_state.irq_list.push_front(fbl::move(irq));
        vector = trusty_get_next_irq(vector + 1, percpu);
    };

    return ZX_OK;
}

static void trusty_irq_probe(uint level) {
    auto release_irq = fbl::MakeAutoCall([] {
        irq_state.irq_list.clear();
    });

    if (trusty_irq_init(false) != ZX_OK) {
        TRACEF("failed to init trusty irq\n");
        return;
    }

    if (trusty_irq_init(true) != ZX_OK) {
        TRACEF("failed to init trusty irq (percpu)\n");
        return;
    }

    release_irq.cancel();
}

LK_INIT_HOOK(trusty_irq, trusty_irq_probe, LK_INIT_LEVEL_PLATFORM);

static int trusty_nop() {
    return (int)arm_smccc_smc(SMC_SC_NOP, 0, 0, 0, 0, 0, 0, 0).x0;
}

static void enable_pending_irqs() {
    uint cpu_num = arch_curr_cpu_num();
    auto& percpu_state = irq_state.percpu[cpu_num];

    TrustyIrq* irq;
    while ((irq = percpu_state.pending_list.pop_front())) {
        irq->UnmaskInterrupt();
    };

    spin_lock(&irq_state.pending_list_lock);
    while ((irq = irq_state.pending_list.pop_front())) {
        irq->UnmaskInterrupt();
    };
    spin_unlock(&irq_state.pending_list_lock);
}

static int __NO_RETURN irq_worker(void* arg) {
    uint cpu_num = arch_curr_cpu_num();
    auto& percpu_state = irq_state.percpu[cpu_num];

    while (true) {
        event_wait(&percpu_state.event);

        while (true) {
            arch_disable_ints();

            enable_pending_irqs();

            int ret = trusty_nop();

            arch_enable_ints();

            if (ret == SM_ERR_NOP_INTERRUPTED) {
                LTRACEF("nop interrupted\n");
                continue;
            }

            if (ret == SM_ERR_NOP_DONE) {
                LTRACEF("nop done\n");
                break;
            }

            TRACEF("trusty_nop() failed, ret=%d\n", ret);
            break;
        }
    }
}

static void trusty_irq_create_worker(uint level) {
    uint cpu_num = arch_curr_cpu_num();
    auto& percpu_state = irq_state.percpu[cpu_num];

    char name[32];
    snprintf(name, sizeof(name), "trusty-irq-worker-%u", cpu_num);
    auto worker = thread_create(name, irq_worker, nullptr, HIGHEST_PRIORITY);
    if (!worker) {
        panic("failed to create irq worker thread for cpu %u!\n", cpu_num);
    }
    thread_set_cpu_affinity(worker, cpu_num_to_mask(cpu_num));

    event_init(&percpu_state.event, false, EVENT_FLAG_AUTOUNSIGNAL);

    thread_resume(worker);
}
LK_INIT_HOOK_FLAGS(trusty_irq_worker, trusty_irq_create_worker, LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_ALL_CPUS);
