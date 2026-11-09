// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// The layout of the Nebula trace memory, a reserved memory region compatible
// with "mediatek,nbl_trace" that the hypervisor, the SOS VMM and the Linux
// guest's nebula_trace driver share. The region starts with an
// nbl_trace_header_t; each tracer's buffer is at the region's base plus the
// header's buf_offs plus the tracer's buf_offs.

// "NblTrace" in ASCII.
#define NBL_TRACE_MAGIC 0x4e626c5472616365ull

// The tracer groups, the index into nbl_trace_header_t::tracers. The Linux
// guest driver uses the same order.
enum {
    GRP_VCPU_EXEC,
    GRP_VCPU_RUN_ACCUM,
    GRP_VCPU_EXIT_REASON,
    GRP_IRQ_COUNT,
    GRP_PCPU_STATUS,
    GRP_VCPU_SCHED_DELAY,
    GRP_VCPU_EXEC_SPLIT,
    GRP_TOP,
    GRP_IRQ_DELAY,
    GRP_KTRACE,
    GRP_COMCAT_LINK,
    GRP_CPU_IPI_DELAY,
    GRP_NBL_REV,
    GRP_TYPE_CNT,
};

#define NBL_TRACER_NAME_LEN 32

typedef struct nbl_tracer_info {
    // The tracer's name.
    char name[NBL_TRACER_NAME_LEN];
    // The offset of the tracer's buffer from the start of the buffer area.
    size_t buf_offs;
    // The size of the tracer's buffer in bytes.
    size_t buf_size;
    // The latest error the hypervisor reported for this tracer.
    int32_t err_code;
    // Whether the hypervisor supports this tracer.
    bool supported;
} nbl_tracer_info_t;

typedef struct nbl_trace_header {
    // NBL_TRACE_MAGIC once the hypervisor has initialized the region.
    uint64_t magic;
    // The offset of the buffer area from the start of the region.
    size_t buf_offs;
    // The trace format version, one byte per component, major first.
    uint32_t version;
    // The number of valid entries in |tracers|.
    uint32_t tracer_cnt;
    nbl_tracer_info_t tracers[GRP_TYPE_CNT];
} nbl_trace_header_t;

__END_CDECLS
