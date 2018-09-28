// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <lib/zircon-internal/ktrace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct ktrace_probe_info ktrace_probe_info_t;

struct ktrace_probe_info {
    ktrace_probe_info_t* next;
    const char* name;
    uint32_t num;
} __ALIGNED(16); // align on multiple of 16 to match linker packing of the ktrace_probe section

void* ktrace_open(uint32_t tag);
void ktrace_tiny(uint32_t tag, uint32_t arg);
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t* data = (uint32_t*) ktrace_open(tag);
    if (data) {
        data[0] = a; data[1] = b; data[2] = c; data[3] = d;
    }
}

static inline void ktrace_ptr(uint32_t tag, const void* ptr, uint32_t c, uint32_t d) {
    ktrace(tag, (uint32_t)((uintptr_t)ptr >> 32), (uint32_t)((uintptr_t)ptr), c, d);
}

#define _ktrace_probe_prologue(_name) \
    static ktrace_probe_info_t info = { NULL, _name, 0 };       \
    __USED __SECTION(".data.rel.ro.ktrace_probe")               \
    static ktrace_probe_info_t *const register_info = &info

#define ktrace_probe0(_name) do {                               \
    _ktrace_probe_prologue(_name);                              \
    ktrace_open(TAG_PROBE_16(info.num));                        \
} while (0)

#define ktrace_probe2(_name,arg0,arg1) do {                  \
    _ktrace_probe_prologue(_name);                           \
    uint32_t* args = (uint32_t*)ktrace_open(TAG_PROBE_24(info.num));    \
    if (args) {                                              \
      args[0] = arg0;                                        \
      args[1] = arg1;                                        \
    }                                                        \
} while (0)

#define ktrace_probe64(_name,arg) do {                  \
    _ktrace_probe_prologue(_name);                           \
    uint64_t* args = (uint64_t*)ktrace_open(TAG_PROBE_24(info.num));    \
    if (args) {                                              \
      *args = arg;                                           \
    }                                                        \
} while (0)

void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always);
static inline void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name) {
    ktrace_name_etc(tag, id, arg, name, false);
}

ssize_t ktrace_read_user(void* ptr, uint32_t off, size_t len);
zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr);

#define KTRACE_DEFAULT_BUFSIZE 32 // MB
#define KTRACE_DEFAULT_GRPMASK 0xFFF

void ktrace_report_live_threads(void);
void ktrace_report_live_processes(void);

__END_CDECLS
