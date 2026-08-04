// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/crashlogger.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>
#include <fdio/util.h>
#include <inspector/inspector.h>
#include <pretty/hexdump.h>

#include "dump-pt.h"

static int verbosity_level = 0;

// If true then s/w breakpoint instructions do not kill the process.
// After the backtrace is printed the thread quietly resumes.
// TODO: The default is on for now for development purposes.
// Ultimately will want to switch this to off.
static bool swbreak_backtrace_enabled = true;

#ifdef __x86_64__
// If true then an attempt is made to dump processor trace data.
// Requires processor tracing turned on in the kernel.
static bool pt_dump_enabled = false;
#endif

// Same as basename, except will not modify |path|.
// This assumes there are no trailing /s.

static const char* cl_basename(const char* path) {
    const char* base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void do_print_error(const char* file, int line, const char* fmt, ...) {
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "crashanalyzer: %s:%d: ", base, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void do_print_zx_error(const char* file, int line, const char* what, zx_status_t status) {
    do_print_error(file, line, "%s: %d (%s)",
                   what, status, zx_status_get_string(status));
}

#define print_zx_error(what, status) \
  do { \
    do_print_zx_error(__FILE__, __LINE__, \
                      (what), static_cast<zx_status_t>(status)); \
  } while (0)

// Return true if the thread is to be resumed "successfully" (meaning the o/s
// won't kill it, and thus the kill process).

static bool is_resumable_swbreak(uint32_t excp_type) {
    if (excp_type == ZX_EXCP_SW_BREAKPOINT && swbreak_backtrace_enabled)
        return true;
    return false;
}

#if defined(__x86_64__)

static uint64_t get_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->rax;
}

#elif defined(__aarch64__)

static uint64_t get_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->r[0];
}

#else

static uint64_t get_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return 0;
}

#endif

static int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    uint64_t magic = get_swbreak_magic(regs);
    return magic == CRASHLOGGER_REQUEST_SELF_BT_ALL_THREADS_MAGIC ||
        magic == CRASHLOGGER_REQUEST_SELF_BT_CURRENT_THREAD_MAGIC;
}

// Return true if the request is for the whole process rather than the thread
// that made it alone.

static bool is_swbreak_request_all_threads(const zx_thread_state_general_regs_t* regs) {
    return get_swbreak_magic(regs) == CRASHLOGGER_REQUEST_SELF_BT_ALL_THREADS_MAGIC;
}

static const char* excp_type_to_str(uint32_t type) {
    switch (type) {
    case ZX_EXCP_GENERAL:
        return "general fault";
    case ZX_EXCP_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case ZX_EXCP_SW_BREAKPOINT:
        return "sw breakpoint";
    case ZX_EXCP_HW_BREAKPOINT:
        return "hw breakpoint";
    case ZX_EXCP_UNALIGNED_ACCESS:
        return "alignment fault";
    case ZX_EXCP_POLICY_ERROR:
        return "policy error";
    default:
        // Note: To get a compilation failure when a new exception type has
        // been added without having also updated this function, compile with
        // -Wswitch-enum.
        return "unknown fault";
    }
}

// How much memory to dump, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
constexpr size_t kMemoryDumpSize = 256;

// The most threads to print a backtrace of. This is an outrageous number of
// threads for one process to have, and space for the koids is allocated on the
// stack, so this can't be too large.
constexpr size_t kMaxThreads = 128;

// How long to wait for a thread to suspend. The timeout is generous because a
// process is usually asked for a backtrace when the machine is already loaded.
constexpr zx_duration_t kSuspendTimeout = ZX_SEC(5);

// Handle of the thread we're dumping.
// This is used by both the main thread and the self-dumper thread.
// However there is no need to lock it as the self-dumper thread only runs
// when the main thread has crashed.
zx_handle_t crashed_thread = ZX_HANDLE_INVALID;

// The exception that |crashed_thread| got.
uint32_t crashed_thread_excp_type;

#if defined(__aarch64__)
static bool write_general_regs(zx_handle_t thread, void* buf, size_t buf_size) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t> (buf_size);
    auto status = zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, buf, to_xfer);
    if (status != ZX_OK) {
        print_zx_error("unable to access general regs", status);
        return false;
    }
    return true;
}
#endif

static void dump_memory(zx_handle_t proc, uintptr_t start, size_t len) {
    // Make sure we're not allocating an excessive amount of stack.
    ZX_DEBUG_ASSERT(len <= kMemoryDumpSize);

    uint8_t buf[len];
    auto res = zx_process_read_memory(proc, start, buf, len, &len);
    if (res < 0) {
        printf("failed reading %p memory; error : %d\n", (void*)start, res);
    } else if (len != 0) {
        hexdump_ex(buf, len, start);
    }
}

static void resume_thread(zx_handle_t thread, bool handled) {
    uint32_t options = ZX_RESUME_EXCEPTION;
    if (!handled)
        options |= ZX_RESUME_TRY_NEXT;
    auto status = zx_task_resume(thread, options);
    if (status != ZX_OK) {
        print_zx_error("unable to \"resume\" thread", status);
        // This shouldn't happen (unless someone killed it already).
        // The task is now effectively hung (until someone kills it).
        // TODO: Try to forcefully kill it ourselves?
    }
}

static void resume_thread_from_exception(zx_handle_t thread,
                                         uint32_t excp_type,
                                         const zx_thread_state_general_regs_t* gregs) {
    if (is_resumable_swbreak(excp_type) &&
        gregs != nullptr && have_swbreak_magic(gregs)) {
#if defined(__x86_64__)
        // On x86, the pc is left at one past the s/w break insn,
        // so there's nothing more we need to do.
#elif defined(__aarch64__)
        zx_thread_state_general_regs_t regs = *gregs;
        // Skip past the brk instruction.
        regs.pc += 4;
        if (!write_general_regs(thread, &regs, sizeof(regs)))
            goto Fail;
#else
        goto Fail;
#endif
        resume_thread(thread, true);
        return;
    }

    // For now, we turn policy exceptions into non-fatal warnings, by
    // resuming the thread when these exceptions occur.  TODO(ZX-922):
    // Remove this and make these exceptions fatal after the system has
    // received some amount of testing with ZX_POL_BAD_HANDLE enabled as a
    // warning.
    if (excp_type == ZX_EXCP_POLICY_ERROR) {
        resume_thread(thread, true);
        return;
    }

#if !defined(__x86_64__)
  Fail:
#endif
    // Tell the o/s to "resume" the thread by killing the process, the
    // exception has not been handled.
    resume_thread(thread, false);
}

static zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        printf("failed to get koid\n");
        return ZX_HANDLE_INVALID;
    }
    return info.koid;
}

// Print the backtrace of |thread|, which must already be stopped.

static void print_thread_backtrace(zx_handle_t process, zx_handle_t thread, zx_koid_t tid,
                                   inspector_dsoinfo_t* dso_list, bool use_libunwind) {
    zx_thread_state_general_regs_t regs;
    if (inspector_read_general_regs(thread, &regs) != ZX_OK)
        return;

    zx_vaddr_t pc, sp, fp;
#if defined(__x86_64__)
    pc = regs.rip;
    sp = regs.rsp;
    fp = regs.rbp;
#elif defined(__aarch64__)
    pc = regs.pc;
    sp = regs.sp;
    fp = regs.r[29];
#else
    // Reading the regs above will likely have failed already, but we don't
    // assume that.
    return;
#endif

    char thread_name[ZX_MAX_NAME_LEN];
    if (zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name)) != ZX_OK) {
        strlcpy(thread_name, "unknown", sizeof(thread_name));
    }

    printf("<== thread %s[%" PRIu64 "], PC at 0x%" PRIxPTR "\n", thread_name, tid, pc);
    inspector_print_general_regs(stdout, &regs, nullptr);
    inspector_print_backtrace(stdout, process, thread, dso_list, pc, sp, fp, use_libunwind);
}

// Print a backtrace of every thread of |process| but |excepting_tid|, whose
// backtrace the caller has already printed.
//
// A thread is suspended only for as long as its own backtrace takes, so this is
// not a single snapshot of the process. The suspension is held with a suspend
// token, which the kernel drops on our exit, so a thread resumes even if we die
// partway through the dump.

static void print_backtraces_of_other_threads(zx_handle_t process, zx_koid_t excepting_tid,
                                              inspector_dsoinfo_t* dso_list, bool use_libunwind) {
    zx_koid_t threads[kMaxThreads];
    size_t num_threads;
    size_t avail;
    zx_status_t status = zx_object_get_info(process, ZX_INFO_PROCESS_THREADS, threads,
                                            sizeof(threads), &num_threads, &avail);
    if (status != ZX_OK) {
        print_zx_error("unable to get the list of threads", status);
        return;
    }
    if (avail > num_threads) {
        printf("WARNING: printing %zu of the process' %zu threads\n", num_threads, avail);
    }

    for (size_t i = 0; i < num_threads; ++i) {
        zx_koid_t tid = threads[i];
        // The thread that made the request is stopped in the exception rather
        // than suspended, and has been printed already.
        if (tid == excepting_tid)
            continue;

        zx_handle_t thread;
        status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status != ZX_OK) {
            print_zx_error("unable to get a handle to a thread, skipping it", status);
            continue;
        }

        zx_handle_t suspend_token;
        status = zx_task_suspend_token(thread, &suspend_token);
        if (status != ZX_OK) {
            print_zx_error("unable to suspend a thread, skipping it", status);
            zx_handle_close(thread);
            continue;
        }

        // Asking to wait for suspension alone would wait until the timeout for
        // a thread that terminates instead, so wait for termination as well.
        zx_signals_t observed = 0u;
        zx_signals_t signals = ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED;
        status = zx_object_wait_one(thread, signals, zx_deadline_after(kSuspendTimeout),
                                    &observed);
        if (status != ZX_OK) {
            print_zx_error("a thread did not suspend, skipping it", status);
        } else if (observed & ZX_THREAD_TERMINATED) {
            printf("Unable to print a backtrace of thread %" PRIu64 ": terminated\n", tid);
        } else {
            print_thread_backtrace(process, thread, tid, dso_list, use_libunwind);
        }

        zx_handle_close(suspend_token);
        zx_handle_close(thread);
    }
}

void process_report(zx_handle_t process, zx_handle_t thread, bool use_libunwind) {
    zx_koid_t pid = get_koid(process);
    zx_koid_t tid = get_koid(thread);

    // Record the crashed thread so that if we crash then self_dump_func
    // can (try to) "resume" the thread so that it's not left hanging.
    crashed_thread = thread;

    zx_exception_report_t report;
    zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                            &report, sizeof(report), NULL, NULL);
    if (status != ZX_OK) {
        printf("failed to get exception report for [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
        zx_handle_close(process);
        zx_handle_close(thread);
        return;
    }

    uint32_t type = report.header.type;

    if (!ZX_EXCP_IS_ARCH(type) && type != ZX_EXCP_POLICY_ERROR)
        return;

    crashed_thread_excp_type = type;
    auto context = report.context;

    zx_thread_state_general_regs_t reg_buf;
    zx_thread_state_general_regs_t *regs = nullptr;
    zx_vaddr_t pc = 0, sp = 0, fp = 0;
    const char* arch = "unknown";
    const char* fatal = "fatal ";

    if (inspector_read_general_regs(thread, &reg_buf) != ZX_OK)
        goto Fail;
    // Delay setting this until here so Fail will know we now have the regs.
    regs = &reg_buf;

#if defined(__x86_64__)
    arch = "x86_64";
    pc = regs->rip;
    sp = regs->rsp;
    fp = regs->rbp;
#elif defined(__aarch64__)
    arch = "aarch64";
    pc = regs->pc;
    sp = regs->sp;
    fp = regs->r[29];
#else
    // It's unlikely we'll get here as trying to read the regs will likely
    // fail, but we don't assume that.
    printf("unsupported architecture .. coming soon.\n");
    goto Fail;
#endif

    // This won't print "fatal" in the case where this is a s/w bkpt but
    // CRASHLOGGER_REQUEST_SELF_BT_MAGIC isn't set. Big deal.
    if (is_resumable_swbreak(type))
        fatal = "";
    // TODO(MA-922): Remove this and make policy exceptions fatal.
    if (type == ZX_EXCP_POLICY_ERROR)
        fatal = "";

    char process_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(process, ZX_PROP_NAME, process_name, sizeof(process_name));
    if (status != ZX_OK) {
        strlcpy(process_name, "unknown", sizeof(process_name));
    }

    char thread_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name));
    if (status != ZX_OK) {
        strlcpy(thread_name, "unknown", sizeof(thread_name));
    }

    printf("<== %sexception: process %s[%" PRIu64 "] thread %s[%" PRIu64 "]\n", fatal,
           process_name, pid, thread_name, tid);
    printf("<== %s, PC at 0x%" PRIxPTR "\n", excp_type_to_str(report.header.type), pc);

#if defined(__x86_64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.x86_64);
#elif defined(__aarch64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.arm_64);

    // Only output the Fault address register and ESR if there's a data fault.
    if (ZX_EXCP_FATAL_PAGE_FAULT == report.header.type) {
        printf(" far %#18" PRIx64 " esr %#18x\n",
               context.arch.u.arm_64.far, context.arch.u.arm_64.esr);
    }
#else
    __UNREACHABLE;
#endif

    printf("bottom of user stack:\n");
    dump_memory(process, sp, kMemoryDumpSize);

    printf("arch: %s\n", arch);

    {
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
        inspector_dso_print_list(stdout, dso_list);
        inspector_print_backtrace(stdout, process, thread, dso_list,
                                  pc, sp, fp, use_libunwind);

        // A request for the whole process asks for the threads that did not
        // make it as well. A crash prints the thread that took it alone: the
        // rest of the process is rarely what killed it, and the report is
        // written before the thread can be resumed to let the process die.
        if (is_resumable_swbreak(type) && is_swbreak_request_all_threads(regs)) {
            print_backtraces_of_other_threads(process, tid, dso_list, use_libunwind);
        }
    }

#ifdef __x86_64__
    if (pt_dump_enabled) {
        try_dump_pt_data();
    }
#endif

Fail:
    if (verbosity_level >= 1)
        printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", pid, tid);

    // allow the thread (and then process) to die, unless the exception is
    // to just trigger a backtrace (if enabled)
    resume_thread_from_exception(thread, type, regs);
    crashed_thread = ZX_HANDLE_INVALID;
    crashed_thread_excp_type = 0u;

    zx_handle_close(thread);
    zx_handle_close(process);
}

int main(int argc, char** argv) {
    // Whether to use libunwind or not.
    // If not then we use a simple algorithm that assumes ABI-specific
    // frame pointers are present.
    bool use_libunwind = true;
#ifdef __x86_64__
    const char* crashanalyzer_pt = getenv("crashanalyzer.pt");
    if (crashanalyzer_pt && strcmp(crashanalyzer_pt, "true") == 0) {
        pt_dump_enabled = true;
    }
#endif

    inspector_set_verbosity(verbosity_level);

    // At debugging level 1 print our dso list (in case we crash in a way
    // that prevents printing it later).
    if (verbosity_level >= 1) {
        zx_handle_t self = zx_process_self();
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(self);
        printf("Crashlogger dso list:\n");
        inspector_dso_print_list(stdout, dso_list);
        inspector_dso_free_list(dso_list);
    }

    zx_handle_t process = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (process == ZX_HANDLE_INVALID) {
        fprintf(stderr, "error: no process in PA_USER0, 0\n");
        return 1;
    }

    zx_handle_t thread = zx_get_startup_handle(PA_HND(PA_USER0, 1));
    if (thread == ZX_HANDLE_INVALID) {
        fprintf(stderr, "error: no thread in PA_USER0, 1\n");
        return 1;
    }

    process_report(process, thread, use_libunwind);
    return 0;
}
