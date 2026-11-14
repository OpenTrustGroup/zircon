// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_dispatcher.h>
#include <zircon/syscalls/port.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <dev/interrupt.h>
#include <fbl/auto_lock.h>
#include <platform.h>

InterruptDispatcher::InterruptDispatcher()
    : timestamp_(0), state_(InterruptState::IDLE) {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

zx_status_t InterruptDispatcher::WaitForInterrupt(zx_time_t* out_timestamp) {
    while (true) {
        bool defer_unmask = false;
        {
            AutoSpinLock guard(&spinlock_);
            if (port_dispatcher_) {
                return ZX_ERR_BAD_STATE;
            }
            switch (state_) {
            case InterruptState::DESTROYED:
                return ZX_ERR_CANCELED;
            case InterruptState::TRIGGERED:
                state_ = InterruptState::NEEDACK;
                *out_timestamp = timestamp_;
                timestamp_ = 0;
                return event_unsignal(&event_);
            case InterruptState::NEEDACK:
                if (flags_ & INTERRUPT_UNMASK_PREWAIT) {
                    UnmaskInterrupt();
                } else if (flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED) {
                    defer_unmask = true;
                }
                break;
            case InterruptState::IDLE:
                break;
            default:
                return ZX_ERR_BAD_STATE;
            }
            state_ = InterruptState::WAITING;
        }
        if (defer_unmask) {
            UnmaskInterrupt();
        }

        zx_status_t status = event_wait_deadline(&event_, ZX_TIME_INFINITE, true);
        if (status != ZX_OK) {
            return status;
        }
    }
}

// The mask here runs under spinlock_, so INTERRUPT_MASK_POSTWAIT is only for
// interrupts whose mask does not block.
bool InterruptDispatcher::SendPacketLocked(zx_time_t timestamp) {
    bool status = port_dispatcher_->QueueInterruptPacket(&port_packet_, timestamp);
    if (flags_ & INTERRUPT_MASK_POSTWAIT) {
        MaskInterrupt();
    }
    timestamp_ = 0;
    return status;
}

zx_status_t InterruptDispatcher::Trigger(zx_time_t timestamp) {

    // TODO(braval): Currently @johngro's driver uses zx_interrupt_trigger
    // to wake up a thread waiting on a physical interrupt
    // Once his driver adopts the interrupt with ports bounded, we can enforce
    // the below condition again
    /*
    if (!(interrupt_.flags & INTERRUPT_VIRTUAL))
        return ZX_ERR_BAD_STATE;
    */

    // Waking a thread under spinlock_ can switch to it at once, and on one CPU
    // a woken thread that then spins on a lock this thread still holds, with
    // interrupts off, spins for good. Hold the switch off until the guard is
    // released, as upstream does; this must come before the guard.
    AutoReschedDisable resched_disable;
    AutoSpinLock guard(&spinlock_);
    // only record timestamp if this is the first signal since we started waiting
    if (!timestamp_) {
        timestamp_ = timestamp;
    }
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }
    if (state_ == InterruptState::NEEDACK && port_dispatcher_) {
        // Cannot trigger a interrupt without ACK
        // only record timestamp if this is the first signal since we started waiting
        return ZX_OK;
    }

    resched_disable.Disable();
    if (port_dispatcher_) {
        SendPacketLocked(timestamp);
        state_ = InterruptState::NEEDACK;
    } else {
        Signal();
        state_ = InterruptState::TRIGGERED;
    }
    return ZX_OK;
}

void InterruptDispatcher::InterruptHandler() {
    AutoSpinLock guard(&spinlock_);

    // Destroy() unregisters this handler only after it marks the interrupt
    // destroyed, so an interrupt may still get here; leave it destroyed.
    if (state_ == InterruptState::DESTROYED) {
        return;
    }

    // only record timestamp if this is the first IRQ since we started waiting
    if (!timestamp_) {
        timestamp_ = current_time();
    }
    if (state_ == InterruptState::NEEDACK && port_dispatcher_) {
        return;
    }
    if (port_dispatcher_) {
        SendPacketLocked(timestamp_);
        state_ = InterruptState::NEEDACK;
    } else {
        Signal();
        state_ = InterruptState::TRIGGERED;
    }
}

zx_status_t InterruptDispatcher::Destroy() {
    // Serialize Destroy() calls (zx_interrupt_destroy(), then the last handle
    // closing), so that the second returns only once the first has masked and
    // unregistered the vector. Create() holds this lock around registering
    // the handler, in the same order.
    fbl::AutoLock destroy_lock(get_lock());

    zx_status_t status = ZX_OK;
    bool wake_waiter = false;
    bool masked = false;
    {
        AutoSpinLock guard(&spinlock_);

        // The second Destroy() must not mask and unregister the vector again:
        // it may have a new owner by now.
        if (state_ == InterruptState::DESTROYED) {
            return ZX_OK;
        }
        if (port_dispatcher_) {
            bool packet_was_in_queue = port_dispatcher_->RemoveInterruptPacket(&port_packet_);
            if ((state_ == InterruptState::NEEDACK) &&
                !packet_was_in_queue) {
                status = ZX_ERR_NOT_FOUND;
            }
        } else {
            wake_waiter = true;
        }
        // Mark it destroyed in every state, TRIGGERED included (an interrupt
        // can fire before Bind()): the handler stays registered until after
        // spinlock_ is released, and must not queue a packet or signal again
        // in between.
        state_ = InterruptState::DESTROYED;

        // An interrupt whose mask does not block is masked here, with
        // interrupts off, as before: the handler leaves a destroyed interrupt
        // alone, so a line that is still asserted would otherwise fire again
        // and again before the mask below, and on one CPU keep this thread
        // from reaching it.
        if (!(flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED)) {
            MaskInterrupt();
            masked = true;
        }
    }

    // A PCI interrupt is masked, and every interrupt unregistered, only once
    // spinlock_ is released: for a PCI interrupt both take the device's mutex,
    // which must not be taken under spinlock_ (see WaitForInterrupt()). A PCI
    // interrupt that fires in between masks itself, and marking it destroyed
    // first keeps a wait or an ack from unmasking it under spinlock_. An unmask
    // deferred until after spinlock_ is released, as only PCI interrupts do,
    // can still land between the mask and the unregister, as upstream; once
    // the handler is gone the PCI driver refuses it.
    if (!masked) {
        MaskInterrupt();
    }
    UnregisterInterruptHandler();

    // Wake a waiter only now, outside spinlock_, so that it cannot run on
    // while the vector is still registered, and may at once create a new
    // interrupt on it.
    if (wake_waiter) {
        Signal();
    }
    return status;
}

zx_status_t InterruptDispatcher::Bind(fbl::RefPtr<PortDispatcher> port_dispatcher,
                                      fbl::RefPtr<InterruptDispatcher> interrupt, uint64_t key) {
    AutoSpinLock guard(&spinlock_);
    if (state_ == InterruptState::DESTROYED) {
        return ZX_ERR_CANCELED;
    }
    if (port_dispatcher_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    if (state_ == InterruptState::WAITING) {
        return ZX_ERR_BAD_STATE;
    }

    port_dispatcher_ = fbl::move(port_dispatcher);
    port_packet_.key = key;
    return ZX_OK;
}

zx_status_t InterruptDispatcher::Ack() {
    zx_status_t status = ZX_OK;
    bool defer_unmask = false;
    {
        // See Trigger(); this must come before the guard.
        AutoReschedDisable resched_disable;
        AutoSpinLock guard(&spinlock_);
        if (port_dispatcher_ == nullptr) {
            return ZX_ERR_BAD_STATE;
        }
        if (state_ == InterruptState::DESTROYED) {
            return ZX_ERR_CANCELED;
        }
        if (state_ == InterruptState::NEEDACK) {
            if (flags_ & INTERRUPT_UNMASK_PREWAIT) {
                UnmaskInterrupt();
            } else if (flags_ & INTERRUPT_UNMASK_PREWAIT_UNLOCKED) {
                defer_unmask = true;
            }
            if (timestamp_) {
                resched_disable.Disable();
                if (!SendPacketLocked(timestamp_)) {
                    // We cannot queue another packet here.
                    // If we reach here it means that the
                    // interrupt packet has not been processed,
                    // another interrupt has occurred & then the
                    // interrupt was ACK'd
                    status = ZX_ERR_BAD_STATE;
                }
            } else {
                state_ = InterruptState::IDLE;
            }
        }
    }
    if (defer_unmask) {
        UnmaskInterrupt();
    }
    return status;
}

void InterruptDispatcher::on_zero_handles() {
    Destroy();
}
