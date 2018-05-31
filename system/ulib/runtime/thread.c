// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/thread.h>

#include <zircon/stack.h>
#include <zircon/syscalls.h>
#include <runtime/mutex.h>
#include <stddef.h>
#include <stdint.h>

// An zxr_thread_t starts its life JOINABLE.
// - If someone calls zxr_thread_join on it, it transitions to JOINED.
// - If someone calls zxr_thread_detach on it, it transitions to DETACHED.
// - When it begins exiting, the EXITING state is entered.
// - When it is no longer using its memory and handle resources, it transitions
//   to DONE.  If the thread was DETACHED prior to EXITING, this transition MAY
//   not happen.
// No other transitions occur.

enum {
    JOINABLE,
    DETACHED,
    JOINED,
    EXITING,
    DONE,
};

typedef struct {
    zxr_thread_entry_t entry;
    zx_handle_t handle;
    atomic_int state;
} zxr_internal_thread_t;

// zxr_thread_t should reserve enough size for our internal data.
_Static_assert(sizeof(zxr_thread_t) == sizeof(zxr_internal_thread_t),
               "Update zxr_thread_t size for this platform.");

static inline zxr_internal_thread_t* to_internal(zxr_thread_t* external) {
  return (zxr_internal_thread_t*)(external);
}

zx_status_t zxr_thread_destroy(zxr_thread_t* thread) {
    zx_handle_t handle = to_internal(thread)->handle;
    to_internal(thread)->handle = ZX_HANDLE_INVALID;
    return handle == ZX_HANDLE_INVALID ? ZX_OK : _zx_handle_close(handle);
}

// Put the thread into EXITING state.  Returns the previous state.
static int begin_exit(zxr_internal_thread_t* thread) {
    return atomic_exchange_explicit(&thread->state, EXITING, memory_order_release);
}

// Claim the thread as JOINED or DETACHED.  Returns true on success, which only
// happens if the previous state was JOINABLE.  Always returns the previous state.
static bool claim_thread(zxr_internal_thread_t* thread, int new_state, int* old_state) {
    *old_state = JOINABLE;
    return atomic_compare_exchange_strong_explicit(
            &thread->state, old_state, new_state,
            memory_order_acq_rel, memory_order_acquire);
}

// Extract the handle from the thread structure.  This must only be called by the thread
// itself (i.e., this is not thread-safe).
static zx_handle_t take_handle(zxr_internal_thread_t* thread) {
    zx_handle_t tmp = thread->handle;
    thread->handle = ZX_HANDLE_INVALID;
    return tmp;
}

static _Noreturn void exit_non_detached(zxr_internal_thread_t* thread) {
    // As soon as thread->state has changed to to DONE, a caller of zxr_thread_join
    // might complete and deallocate the memory containing the thread descriptor.
    // Hence it's no longer safe to touch *thread or read anything out of it.
    // Therefore we must extract the thread handle before that transition
    // happens.
    zx_handle_t handle = take_handle(thread);

    // Wake the _zx_futex_wait in zxr_thread_join (below), and then die.
    // This has to be done with the special four-in-one vDSO call because
    // as soon as the state transitions to DONE, the joiner is free to unmap
    // our stack out from under us.  Note there is a benign race here still: if
    // the address is unmapped and our futex_wake fails, it's OK; if the memory
    // is reused for something else and our futex_wake tickles somebody
    // completely unrelated, well, that's why futex_wait can always have
    // spurious wakeups.
    _zx_futex_wake_handle_close_thread_exit(&thread->state, 1, DONE, handle);
    __builtin_trap();
}

static _Noreturn void thread_trampoline(uintptr_t ctx, uintptr_t arg) {
    zxr_internal_thread_t* thread = (zxr_internal_thread_t*)ctx;

    thread->entry((void*)arg);

    int old_state = begin_exit(thread);
    switch (old_state) {
    case JOINABLE:
        // Nobody's watching right now, but they might start watching as we
        // exit.  Just in case, behave as if we've been joined and wake the
        // futex on our way out.
    case JOINED:
        // Somebody loves us!  Or at least intends to inherit when we die.
        exit_non_detached(thread);
        break;
    }

    // Cannot be in DONE, EXITING, or DETACHED and reach here.  For DETACHED, it
    // is the responsibility of a higher layer to ensure this is not reached.
    __builtin_trap();
}

_Noreturn void zxr_thread_exit_unmap_if_detached(
    zxr_thread_t* thread, zx_handle_t vmar, uintptr_t addr, size_t len) {

    int old_state = begin_exit(to_internal(thread));
    switch (old_state) {
    case DETACHED: {
        zx_handle_t handle = take_handle(to_internal(thread));
        _zx_vmar_unmap_handle_close_thread_exit(vmar, addr, len, handle);
        break;
    }
    // See comments in thread_trampoline.
    case JOINABLE:
    case JOINED:
        exit_non_detached(to_internal(thread));
        break;
    }

    // Cannot be in DONE or the EXITING and reach here.
    __builtin_trap();
}

// Local implementation so libruntime does not depend on libc.
static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (*s++ != '\0')
        ++len;
    return len;
}

static void initialize_thread(zxr_internal_thread_t* thread,
                              zx_handle_t handle, bool detached) {
    *thread = (zxr_internal_thread_t){ .handle = handle, };
    atomic_init(&thread->state, detached ? DETACHED : JOINABLE);
}

zx_status_t zxr_thread_create(zx_handle_t process, const char* name,
                              bool detached, zxr_thread_t* thread) {
    initialize_thread(to_internal(thread), ZX_HANDLE_INVALID, detached);
    if (name == NULL)
        name = "";
    size_t name_length = local_strlen(name) + 1;
    return _zx_thread_create(process, name, name_length, 0, &to_internal(thread)->handle);
}

zx_status_t zxr_thread_start(zxr_thread_t* thread, uintptr_t stack_addr, size_t stack_size, zxr_thread_entry_t entry, void* arg) {
    to_internal(thread)->entry = entry;

    // compute the starting address of the stack
    uintptr_t sp = compute_initial_stack_pointer(stack_addr, stack_size);

    // kick off the new thread
    zx_status_t status = _zx_thread_start(to_internal(thread)->handle,
                                          (uintptr_t)thread_trampoline, sp,
                                          (uintptr_t)thread, (uintptr_t)arg);

    if (status != ZX_OK)
        zxr_thread_destroy(thread);
    return status;
}

static void wait_for_done(zxr_internal_thread_t* thread, int32_t old_state) {
    do {
        switch (_zx_futex_wait(&thread->state, old_state, ZX_TIME_INFINITE)) {
            case ZX_ERR_BAD_STATE:   // Never blocked because it had changed.
            case ZX_OK:              // Woke up because it might have changed.
                old_state = atomic_load_explicit(&thread->state,
                                                 memory_order_acquire);
                break;
            default:
                __builtin_trap();
        }
        // Wait until we reach the DONE state, even if we observe the
        // intermediate EXITING state.
    } while (old_state == JOINED || old_state == EXITING);

    if (old_state != DONE)
        __builtin_trap();
}

zx_status_t zxr_thread_join(zxr_thread_t* external_thread) {
    zxr_internal_thread_t* thread = to_internal(external_thread);

    int old_state;
    // Try to claim the join slot on this thread.
    if (claim_thread(thread, JOINED, &old_state)) {
        wait_for_done(thread, JOINED);
    } else {
        switch (old_state) {
            case JOINED:
            case DETACHED:
                return ZX_ERR_INVALID_ARGS;
            case EXITING:
                // Since it is undefined to call zxr_thread_join on a thread
                // that has already been detached or joined, we assume the state
                // prior to EXITING was JOINABLE, and act as if we had
                // successfully transitioned to JOINED.
                wait_for_done(thread, EXITING);
                // Fall-through to DONE case
            case DONE:
                break;
            default:
                __builtin_trap();
        }
    }

    // The thread has already closed its own handle.
    return ZX_OK;
}

zx_status_t zxr_thread_detach(zxr_thread_t* thread) {
    int old_state;
    // Try to claim the join slot on this thread on behalf of the thread.
    if (!claim_thread(to_internal(thread), DETACHED, &old_state)) {
        switch (old_state) {
            case DETACHED:
            case JOINED:
                return ZX_ERR_INVALID_ARGS;
            case EXITING: {
                // Since it is undefined behavior to call zxr_thread_detach on a
                // thread that has already been detached or joined, we assume
                // the state prior to EXITING was JOINABLE.  However, since the
                // thread is already shutting down, it is too late to tell it to
                // clean itself up.  Since the thread is still running, we cannot
                // just return ZX_ERR_BAD_STATE, which would suggest we couldn't detach and
                // the thread has already finished running.  Instead, we call join,
                // which will return soon due to the thread being actively shutting down,
                // and then return ZX_ERR_BAD_STATE to tell the caller that they
                // must manually perform any post-join work.
                zx_status_t ret = zxr_thread_join(thread);
                if (unlikely(ret != ZX_OK)) {
                    if (unlikely(ret != ZX_ERR_INVALID_ARGS)) {
                        __builtin_trap();
                    }
                    return ret;
                }
                // Fall-through to DONE case.
                __FALLTHROUGH;
            }
            case DONE:
                return ZX_ERR_BAD_STATE;
            default:
                __builtin_trap();
        }
    }

    return ZX_OK;
}

bool zxr_thread_detached(zxr_thread_t* thread) {
    int state = atomic_load_explicit(&to_internal(thread)->state, memory_order_acquire);
    return state == DETACHED;
}

zx_handle_t zxr_thread_get_handle(zxr_thread_t* thread) {
    return to_internal(thread)->handle;
}

zx_status_t zxr_thread_adopt(zx_handle_t handle, zxr_thread_t* thread) {
    initialize_thread(to_internal(thread), handle, false);
    return handle == ZX_HANDLE_INVALID ? ZX_ERR_BAD_HANDLE : ZX_OK;
}
