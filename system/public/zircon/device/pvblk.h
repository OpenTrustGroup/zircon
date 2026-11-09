// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The paravirtual block device.  A guest drives it through a small MMIO
// register window that the VMM emulates, and through submission and
// completion rings in guest memory that the block driver of the host reads
// and writes directly.  The VMM forwards ring setup and doorbells to the
// block driver with the IOCTL_PVBLK_* ioctls.
//
// The rings, their entries and struct pvblk_config are shared with the guest
// and must keep their layout; PVBLK_ABI_VERSION changes with any change.

#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The pvblk ioctls share the block family.  <zircon/device/block.h> uses
// numbers 1 to 19 and <zircon/device/vblock.h> 0x80 to 0x8c, so these take
// 0x90 to 0x96.

// Start serving a guest.  in: struct pvblk_start_param.
#define IOCTL_PVBLK_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x90)
// Hand over a VMO covering the started guest's memory.  in: the VMO handle,
// which the driver takes.
#define IOCTL_PVBLK_SET_GPA_RANGE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_BLOCK, 0x91)
// Hand over the event the driver signals on completions.  in: the event
// handle, which the driver takes.
#define IOCTL_PVBLK_SET_EVENT \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_BLOCK, 0x92)
// Attach the rings of one queue.  in: struct pvblk_ring_param.
#define IOCTL_PVBLK_SET_RING \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x93)
// Process new submissions of one queue.  in: struct pvblk_doorbell_param.
#define IOCTL_PVBLK_DOORBELL \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x94)
// Get the device configuration.  in: struct pvblk_config_param; out: struct
// pvblk_config.
#define IOCTL_PVBLK_GET_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x95)
// Stop serving a guest.  in: struct pvblk_config_param or a uint16_t vmid.
#define IOCTL_PVBLK_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x96)

// Identification of struct pvblk_config.
#define PVBLK_MAGIC 0x4b4c4250U  // "PBLK"
#define PVBLK_ABI_VERSION 1U

// Queue limits.  Queue depths are powers of two.
#define PVBLK_MAX_QUEUES 1U
#define PVBLK_MIN_QUEUE_DEPTH 2U
#define PVBLK_MAX_QUEUE_DEPTH 256U

// The number of data segments one submission carries.
#define PVBLK_MAX_INLINE_SEGS 16U

// The largest argument of a control ioctl, in bytes.
#define PVBLK_CONTROL_ARG_MAX 4096U

static inline bool pvblk_queue_depth_valid(uint32_t depth) {
    return depth >= PVBLK_MIN_QUEUE_DEPTH && depth <= PVBLK_MAX_QUEUE_DEPTH &&
           (depth & (depth - 1)) == 0;
}

// The index mask of a queue of |depth| entries.
static inline uint32_t pvblk_queue_mask(uint32_t depth) {
    return depth - 1;
}

// Whether a submission's sequence number |sqe_seq| is the one its slot
// expects, |slot_seq|.  A stale or replayed submission does not match.
static inline bool pvblk_seq_matches(uint32_t slot_seq, uint32_t sqe_seq) {
    return slot_seq == sqe_seq;
}

// Inline encryption on the wire.  The mode list and the data unit number
// size follow <zircon/device/blk_crypto.h>.
#define PVBLK_CRYPTO_KEY_SLOT_NUM 32U
#define PVBLK_CRYPTO_MODE_COUNT 5U
#define PVBLK_CRYPTO_DUN_ARRAY_SIZE 4U
#define PVBLK_CRYPTO_MAX_KEY_SIZE 128U

struct pvblk_crypto_cap {
    uint32_t max_dun_bytes_supported;
    uint32_t key_types_supported;
    uint32_t modes_supported[PVBLK_CRYPTO_MODE_COUNT];
};

struct pvblk_crypto_config {
    uint32_t crypto_mode;
    uint32_t data_unit_size;
    uint32_t dun_bytes;
    uint32_t key_type;
};

struct pvblk_crypto_key {
    struct pvblk_crypto_config crypto_cfg;
    uint32_t data_unit_size_bits;
    uint32_t size;
    uint8_t raw[PVBLK_CRYPTO_MAX_KEY_SIZE];
};

// Request operations; the values follow the virtio block request types.
#define PVBLK_OP_READ 0U
#define PVBLK_OP_WRITE 1U
#define PVBLK_OP_FLUSH 4U
#define PVBLK_OP_DISCARD 11U
#define PVBLK_OP_WRITE_ZEROES 13U
#define PVBLK_OP_SECURE_ERASE 14U

// Submission flags.
#define PVBLK_SQE_F_USE_CRYPTO (1U << 0)
#define PVBLK_SQE_F_WRITE_ZEROES_UNMAP (1U << 1)

// Completion status.
enum pvblk_status {
    PVBLK_STS_OK = 0,
    PVBLK_STS_IOERR = 1,
    PVBLK_STS_UNSUPP = 2,
    PVBLK_STS_RANGE = 3,
};

// One data segment in guest memory.
struct pvblk_segment {
    uint64_t gpa;
    uint32_t len;
    uint32_t reserved;
};

// A submission queue entry.  Everything before crypto_dun is always valid;
// the crypto fields only with PVBLK_SQE_F_USE_CRYPTO, and the first
// nr_segments entries of segs.
struct pvblk_sqe {
    uint16_t qid;
    uint16_t slot;
    uint32_t seq;
    uint16_t op;
    uint16_t flags;
    uint16_t nr_segments;
    uint16_t reserved;
    // The length in bytes.
    uint32_t bytes;
    uint32_t reserved2;
    // The device offset in 512-byte sectors.
    uint64_t sector;
    uint64_t crypto_dun[PVBLK_CRYPTO_DUN_ARRAY_SIZE];
    struct pvblk_crypto_key crypto_key;
    uint32_t crypto_key_index;
    // Bit 0 toggles each time the guest replaces the key in
    // crypto_key_index.
    uint32_t crypto_bmap;
    struct pvblk_segment segs[PVBLK_MAX_INLINE_SEGS];
};

// A completion queue entry, echoing the qid, slot and seq of its submission.
struct pvblk_cqe {
    uint16_t qid;
    uint16_t slot;
    uint32_t seq;
    uint32_t status;
    uint32_t bytes_done;
};

// The ring indices of one queue.  The guest produces sq_tail and cq_head;
// the host produces sq_head and cq_tail.
struct pvblk_queue_ctrl {
    uint32_t sq_head;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint32_t cq_tail;
    // Written by the guest: PVBLK_IRQ_ARM_MODE plus a sequence number that
    // changes each time it arms the completion interrupt.
    uint32_t irq_seq;
    uint32_t queue_state;
};

#define PVBLK_IRQ_ARM_MODE 0x80000000U
#define PVBLK_QUEUE_READY 1U

// Feature bits of struct pvblk_config.
#define PVBLK_FEATURE_READ_WRITE (1ULL << 0)
#define PVBLK_FEATURE_FLUSH (1ULL << 1)
#define PVBLK_FEATURE_POLL_THEN_IRQ (1ULL << 2)
#define PVBLK_FEATURE_IRQ_ARM (1ULL << 3)
#define PVBLK_FEATURE_CONFIG_WCE (1ULL << 4)
#define PVBLK_FEATURE_TOPOLOGY (1ULL << 5)
#define PVBLK_FEATURE_SIZE_MAX (1ULL << 6)
#define PVBLK_FEATURE_SEG_MAX (1ULL << 7)
#define PVBLK_FEATURE_DISCARD (1ULL << 8)
#define PVBLK_FEATURE_WRITE_ZEROES (1ULL << 9)
#define PVBLK_FEATURE_SECURE_ERASE (1ULL << 10)
#define PVBLK_FEATURE_READ_ONLY (1ULL << 11)
#define PVBLK_FEATURE_UFS_LUN (1ULL << 12)
#define PVBLK_FEATURE_INLINE_CRYPTO (1ULL << 13)
#define PVBLK_FEATURE_SERIAL (1ULL << 14)

#define PVBLK_SERIAL_LEN 20U

// The device configuration.
struct pvblk_config {
    uint32_t magic;
    uint32_t version;
    uint64_t feature_bits;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t queue_count;
    uint32_t queue_depth;
    uint32_t max_segments;
    uint32_t max_segment_size;
    uint32_t physical_block_size;
    uint32_t alignment_offset;
    uint32_t io_min;
    uint32_t io_opt;
    uint32_t discard_max_sectors;
    uint32_t discard_max_segments;
    uint32_t discard_granularity;
    uint32_t write_zeroes_max_sectors;
    uint32_t write_zeroes_max_segments;
    uint32_t secure_erase_max_sectors;
    uint32_t secure_erase_max_segments;
    uint32_t secure_erase_granularity;
    uint32_t ufs_lun;
    uint8_t wce;
    uint8_t read_only;
    uint8_t serial[PVBLK_SERIAL_LEN];
    uint8_t reserved[2];
    struct pvblk_crypto_cap crypto_cap;
};

// A range of guest physical addresses.  |end| is inclusive.
struct pvblk_mem_region {
    uint64_t start;
    uint64_t end;
};

// The argument of IOCTL_PVBLK_START.
struct pvblk_start_param {
    uint16_t vmid;
    uint16_t queue_count;
    uint32_t queue_depth;
    struct pvblk_mem_region mem;
};

// The argument of IOCTL_PVBLK_GET_CONFIG and IOCTL_PVBLK_STOP.
struct pvblk_config_param {
    uint16_t vmid;
};

// The argument of IOCTL_PVBLK_SET_RING: the guest physical addresses of a
// struct pvblk_queue_ctrl and of |depth| submission and completion entries.
struct pvblk_ring_param {
    uint16_t vmid;
    uint16_t qid;
    uint32_t depth;
    uint64_t ctrl_gpa;
    uint64_t sq_gpa;
    uint64_t cq_gpa;
};

// The argument of IOCTL_PVBLK_DOORBELL.
struct pvblk_doorbell_param {
    uint16_t vmid;
    uint16_t qid;
    // The sequence number the guest wrote with the doorbell.
    uint32_t doorbell_seq;
    // The most submissions to take, or zero for the queue depth.
    uint32_t drain_budget;
};

// The MMIO register window, 32-bit registers at these offsets.
#define PVBLK_MMIO_SIZE 0x1000U
#define PVBLK_MMIO_MAGIC 0x4b4c4250U  // "PBLK"
#define PVBLK_MMIO_VERSION 1U

#define PVBLK_REG_MAGIC 0x000U
#define PVBLK_REG_VERSION 0x004U
#define PVBLK_REG_FEATURES_LO 0x008U
#define PVBLK_REG_FEATURES_HI 0x00cU
#define PVBLK_REG_BLOCK_COUNT_LO 0x010U
#define PVBLK_REG_BLOCK_COUNT_HI 0x014U
#define PVBLK_REG_BLOCK_SIZE 0x018U
#define PVBLK_REG_QUEUE_COUNT 0x01cU
#define PVBLK_REG_QUEUE_DEPTH 0x020U
#define PVBLK_REG_MAX_SEGS 0x024U
#define PVBLK_REG_MAX_SEGMENT_SIZE 0x028U
#define PVBLK_REG_STATUS 0x02cU
#define PVBLK_REG_COMMAND 0x030U
#define PVBLK_REG_SELECTED_Q 0x034U
#define PVBLK_REG_CTRL_GPA_LO 0x038U
#define PVBLK_REG_CTRL_GPA_HI 0x03cU
#define PVBLK_REG_SQ_GPA_LO 0x040U
#define PVBLK_REG_SQ_GPA_HI 0x044U
#define PVBLK_REG_CQ_GPA_LO 0x048U
#define PVBLK_REG_CQ_GPA_HI 0x04cU
#define PVBLK_REG_RING_DEPTH 0x050U
#define PVBLK_REG_DOORBELL 0x054U
#define PVBLK_REG_DOORBELL_SEQ 0x058U
#define PVBLK_REG_WRITEBACK 0x05cU
#define PVBLK_REG_READ_ONLY 0x060U
#define PVBLK_REG_PHYSICAL_BLOCK_SIZE 0x064U
#define PVBLK_REG_ALIGNMENT_OFFSET 0x068U
#define PVBLK_REG_IO_MIN 0x06cU
#define PVBLK_REG_IO_OPT 0x070U
#define PVBLK_REG_DISCARD_MAX_SECTORS 0x074U
#define PVBLK_REG_DISCARD_MAX_SEGS 0x078U
#define PVBLK_REG_DISCARD_GRANULARITY 0x07cU
#define PVBLK_REG_WRITE_ZEROES_MAX_SECTORS 0x080U
#define PVBLK_REG_WRITE_ZEROES_MAX_SEGS 0x084U
#define PVBLK_REG_SECURE_ERASE_MAX_SECTORS 0x088U
#define PVBLK_REG_SECURE_ERASE_MAX_SEGS 0x08cU
#define PVBLK_REG_SECURE_ERASE_GRANULARITY 0x090U
#define PVBLK_REG_UFS_LUN 0x094U
#define PVBLK_REG_SERIAL_0 0x098U
#define PVBLK_REG_SERIAL_1 0x09cU
#define PVBLK_REG_SERIAL_2 0x0a0U
#define PVBLK_REG_SERIAL_3 0x0a4U
#define PVBLK_REG_SERIAL_4 0x0a8U
#define PVBLK_REG_CONTROL_REQUEST 0x0acU
#define PVBLK_REG_CONTROL_ARG_LO 0x0b0U
#define PVBLK_REG_CONTROL_ARG_HI 0x0b4U
#define PVBLK_REG_CONTROL_ARG_LEN 0x0b8U
#define PVBLK_REG_CONTROL_RESULT 0x0bcU
#define PVBLK_REG_CONTROL_STATUS 0x0c0U
#define PVBLK_REG_CRYPTO_MAX_DUN_BYTES 0x0c4U
#define PVBLK_REG_CRYPTO_KEY_TYPES 0x0c8U
// PVBLK_CRYPTO_MODE_COUNT registers, one per encryption mode.
#define PVBLK_REG_CRYPTO_MODES_BASE 0x0ccU

// Bits of PVBLK_REG_STATUS.
#define PVBLK_STATUS_READY (1U << 0)
#define PVBLK_STATUS_IRQ_READY (1U << 1)
#define PVBLK_STATUS_RING_READY (1U << 2)

// Values written to PVBLK_REG_COMMAND.
#define PVBLK_CMD_SET_RING 1U
#define PVBLK_CMD_STOP 2U
// Run the ioctl in PVBLK_REG_CONTROL_REQUEST on the argument at
// PVBLK_REG_CONTROL_ARG_LO/HI; the result lands in PVBLK_REG_CONTROL_STATUS.
#define PVBLK_CMD_CONTROL_IOCTL 3U

__END_CDECLS
