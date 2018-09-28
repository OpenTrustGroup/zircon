// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <lib/fdio/limits.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

__BEGIN_CDECLS

// clang-format off

// Fuchsia-io limits.
//
// TODO(FIDL-127): Compute these values with the "union of all fuchsia-io"
// messages.
#define ZXFIDL_MAX_MSG_BYTES    (FDIO_CHUNK_SIZE * 2)
#define ZXFIDL_MAX_MSG_HANDLES  (FDIO_MAX_HANDLES)

// indicates the callback is taking responsibility for the
// channel receiving incoming messages.
//
// Unlike ERR_DISPATCHER_INDIRECT, this callback is propagated
// through the zxfidl_handlers.
#define ERR_DISPATCHER_ASYNC ZX_ERR_ASYNC

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE ZX_ERR_STOP

// callback to process a FIDL message.
// - |msg| is a decoded FIDL message.
// - return value of ERR_DISPATCHER_{INDIRECT,ASYNC} indicates that the reply is
//   being handled by the callback (forwarded to another server, sent later,
//   etc, and no reply message should be sent).
// - WARNING: Once this callback returns, usage of |msg| is no longer
//   valid. If a client transmits ERR_DISPATCHER_{INDIRECT,ASYNC}, and intends
//   to respond asynchronously, they must copy the fields of |msg| they
//   wish to use at a later point in time.
// - otherwise, the return value is treated as the status to send
//   in the rpc response, and msg.len indicates how much valid data
//   to send.  On error return msg.len will be set to 0.
typedef zx_status_t (*zxfidl_cb_t)(fidl_msg_t* msg, fidl_txn_t* txn,
                                   void* cookie);

//TODO: this really should be private to fidl.c, but is used by libfs
typedef struct zxfidl_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
} zxfidl_connection_t;

static_assert(offsetof(zxfidl_connection_t, txn) == 0,
              "Connection must transparently be a fidl_txn");

inline zxfidl_connection_t zxfidl_txn_copy(fidl_txn_t* txn) {
    return *(zxfidl_connection_t*) txn;
}

// A fdio_dispatcher_handler suitable for use with a fdio_dispatcher.
zx_status_t zxfidl_handler(zx_handle_t h, zxfidl_cb_t cb, void* cookie);

// OPEN and CLONE ops do not return a reply
// Instead they receive a channel handle that they write their status
// and (if successful) type, extra data, and handles to.

typedef struct {
    uint32_t tag;
    uint32_t reserved;
    union {
        zx_handle_t handle;
        struct {
            zx_handle_t e;
        } file;
        struct {
            zx_handle_t s;
        } pipe;
        struct {
            zx_handle_t v;
            uint64_t offset;
            uint64_t length;
        } vmofile;
        struct {
            zx_handle_t e;
        } device;
    };
} zxrio_node_info_t;

#define ZXRIO_DESCRIBE_HDR_SZ       (__builtin_offsetof(zxrio_describe_t, extra))

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
typedef struct {
    fidl_message_header_t hdr;
    zx_status_t status;
    zxrio_node_info_t* extra_ptr;
    zxrio_node_info_t extra;
} zxrio_describe_t;

#define FDIO_MMAP_FLAG_READ    (1u << 0)
#define FDIO_MMAP_FLAG_WRITE   (1u << 1)
#define FDIO_MMAP_FLAG_EXEC    (1u << 2)
// Require a copy-on-write clone of the underlying VMO.
// The request should fail if the VMO is not cloned.
// May not be supplied with FDIO_MMAP_FLAG_EXACT.
#define FDIO_MMAP_FLAG_PRIVATE (1u << 16)
// Require an exact (non-cloned) handle to the underlying VMO.
// The request should fail if a handle to the exact VMO
// is not returned.
// May not be supplied with FDIO_MMAP_FLAG_PRIVATE.
#define FDIO_MMAP_FLAG_EXACT   (1u << 17)

static_assert(FDIO_MMAP_FLAG_READ == ZX_VM_PERM_READ, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_WRITE == ZX_VM_PERM_WRITE, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_EXEC == ZX_VM_PERM_EXECUTE, "Vmar / Mmap flags should be aligned");

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

#define READDIR_CMD_NONE  0
#define READDIR_CMD_RESET 1

__END_CDECLS
