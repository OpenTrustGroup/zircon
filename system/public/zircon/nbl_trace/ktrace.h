// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/ktrace.h>
#include <zircon/nbl_trace/nbl_trace.h>

__BEGIN_CDECLS

// Records in the GRP_KTRACE tracer's ring buffer of the Nebula trace memory.
// They use the tag layout of <zircon/ktrace.h>.

// The number of trace events a ktrace ring buffer can name, and the length of
// each name. The Linux guest's nebula_trace driver uses the same values.
#define TRACE_EVENT_CNT 32
#define EVENT_NAME_LEN_MAX 32

// The event of a filler record that pads the ring buffer up to its end, so
// that no record wraps around.
#define DUMMY_EVT 0xfff

// The size in bytes of the metadata area at the start of a ring buffer in
// snapshot mode. Metadata records beyond it are dropped.
#define KTRACE_META_LEN 0x1000

// The group of the communication records the VMM writes.
#define KTRACE_GRP_NBL_COMM 0x400

// A record with a timestamp, the writing thread and CPU, and four 64-bit
// arguments. |meta_mask| packs, from bit 40 down to bit 0 in steps of 8 bits,
// the category, the event and the kinds of arguments a, b, c and d.
typedef struct ktrace_rec_comm {
    uint32_t tag;
    uint32_t tid;
    uint64_t ts;
    uint32_t curr_cpu;
    uint32_t reserved;
    uint64_t meta_mask;
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
} ktrace_rec_comm_t;

static_assert(sizeof(ktrace_rec_comm_t) == 64, "ktrace_rec_comm_t is not 64 bytes");

// The VMM enters and leaves the handling of a guest request.
#define TAG_COMM_ENTER \
    ((uint32_t)KTRACE_TAG(0x300, KTRACE_GRP_NBL_COMM, sizeof(ktrace_rec_comm_t)))
#define TAG_COMM_EXIT \
    ((uint32_t)KTRACE_TAG(0x301, KTRACE_GRP_NBL_COMM, sizeof(ktrace_rec_comm_t)))

__END_CDECLS
