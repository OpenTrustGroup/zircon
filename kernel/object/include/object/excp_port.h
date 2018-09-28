// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <arch/exception.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <object/dispatcher.h>

#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

class ThreadDispatcher;
class ProcessDispatcher;
class PortDispatcher;

// Represents the binding of an exception port to a specific target
// (job/process/thread). Multiple ExceptionPorts may exist for a
// single underlying PortDispatcher.
class ExceptionPort : public fbl::DoublyLinkedListable<fbl::RefPtr<ExceptionPort>>
                    , public fbl::RefCounted<ExceptionPort> {
public:
    enum class Type { NONE, JOB_DEBUGGER, DEBUGGER, THREAD, PROCESS, JOB};

    static zx_status_t Create(Type type, fbl::RefPtr<PortDispatcher> port,
                              uint64_t port_key,
                              fbl::RefPtr<ExceptionPort>* eport);
    ~ExceptionPort();

    Type type() const { return type_; }

    zx_status_t SendPacket(ThreadDispatcher* thread, uint32_t type);

    void OnThreadStartForDebugger(ThreadDispatcher* thread);
    void OnThreadExitForDebugger(ThreadDispatcher* thread);
    void OnProcessStartForDebugger(ThreadDispatcher* thread);

    // Records the target that the ExceptionPort is bound to, so it can
    // unbind when the underlying PortDispatcher dies.
    void SetTarget(const fbl::RefPtr<JobDispatcher>& target);
    void SetTarget(const fbl::RefPtr<ProcessDispatcher>& target);
    void SetTarget(const fbl::RefPtr<ThreadDispatcher>& target);

    // Drops the ExceptionPort's references to its target and PortDispatcher.
    // Called by the target when the port is explicitly unbound.
    void OnTargetUnbind();

    // Validates that this eport is associated with the given instance.
    bool PortMatches(const PortDispatcher* port, bool allow_null);

    static void BuildArchReport(zx_exception_report_t* report, uint32_t type,
                                const arch_exception_context_t* arch_context);

private:
    friend class PortDispatcher;

    ExceptionPort(Type type, fbl::RefPtr<PortDispatcher> port, uint64_t port_key);

    ExceptionPort(const ExceptionPort&) = delete;
    ExceptionPort& operator=(const ExceptionPort&) = delete;

    zx_status_t SendPacketWorker(uint32_t type, zx_koid_t pid, zx_koid_t tid);

    // Unbinds from the target if bound, and drops the ref to |port_|.
    // Called by |port_| when it reaches zero handles.
    void OnPortZeroHandles();

    // Returns true if the ExceptionPort is currently bound to a target.
    bool IsBoundLocked() const TA_REQ(lock_) {
        return target_ != nullptr;
    }

    static void BuildReport(zx_exception_report_t* report, uint32_t type);

    fbl::Canary<fbl::magic("EXCP")> canary_;

    // These aren't locked as once the exception port is created these are
    // immutable (the port itself has its own locking though).
    const Type type_;
    const uint64_t port_key_;

    // The underlying port. If null, the ExceptionPort has been unbound.
    fbl::RefPtr<PortDispatcher> port_ TA_GUARDED(lock_);

    // The target of the exception port.
    // The system exception port doesn't have a Dispatcher, hence the bool.
    fbl::RefPtr<Dispatcher> target_ TA_GUARDED(lock_);

    DECLARE_MUTEX(ExceptionPort) lock_;

    // NOTE: The DoublyLinkedListNodeState is guarded by |port_|'s lock,
    // and should only be touched using port_->LinkExceptionPort()
    // or port_->UnlinkExceptionPort(). This goes for ::InContainer(), too.
};
