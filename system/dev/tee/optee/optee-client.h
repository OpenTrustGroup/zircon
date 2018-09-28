// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <fbl/intrusive_double_list.h>
#include <zircon/device/tee.h>

#include "optee-controller.h"

namespace optee {

class OpteeClient;
using OpteeClientBase = ddk::Device<OpteeClient, ddk::Closable, ddk::Ioctlable>;
using OpteeClientProtocol = ddk::TeeProtocol<OpteeClient>;

// The Optee driver allows for simultaneous access from different processes. The OpteeClient object
// is a distinct device instance for each client connection. This allows for per-instance state to
// be managed together. For example, if a client closes the device, OpteeClient can free all of the
// allocated shared memory buffers and sessions that were created by that client without interfering
// with other active clients.

class OpteeClient : public OpteeClientBase,
                    public OpteeClientProtocol,
                    public fbl::DoublyLinkedListable<OpteeClient*> {
public:
    explicit OpteeClient(OpteeController* controller)
        : OpteeClientBase(controller->zxdev()), controller_(controller) {}

    OpteeClient(const OpteeClient&) = delete;
    OpteeClient& operator=(const OpteeClient&) = delete;

    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

    // If the Controller is unbound, we need to notify all clients that the device is no longer
    // available. The Controller will invoke this function so that any subsequent calls on the
    // client will notify the caller that the peer has closed.
    void MarkForClosing() { needs_to_close_ = true; }

    // IOCTLs
    zx_status_t OpenSession(const tee_ioctl_session_request_t* session_request,
                            tee_ioctl_session_t* out_session,
                            size_t* out_actual);

private:
    using SharedMemoryList = fbl::DoublyLinkedList<fbl::unique_ptr<SharedMemory>>;

    zx_status_t ConvertIoctlParamsToOpteeParams(const tee_ioctl_param_t* params,
                                                size_t num_params,
                                                fbl::Array<MessageParam>* out_optee_params);

    // Attempts to allocate a block of SharedMemory from a designated memory pool.
    //
    // On success:
    //  * Tracks the allocated memory block in the allocated_shared_memory_ list.
    //  * Gives the physical address of the memory block in out_phys_addr
    //  * Gives an identifier for the memory block in out_mem_id. This identifier will later be
    //    used to free the memory block.
    //
    // On failure:
    //  * Sets the physical address of the memory block to 0.
    //  * Sets the identifier of the memory block to 0.
    template <typename SharedMemoryPoolTraits>
    zx_status_t AllocateSharedMemory(size_t size,
                                     SharedMemoryPool<SharedMemoryPoolTraits>* memory_pool,
                                     zx_paddr_t* out_phys_addr,
                                     uint64_t* out_mem_id);

    // Frees a block of SharedMemory that was previously allocated by the driver.
    //
    // Parameters:
    //  * mem_id:   The identifier for the memory block to free, given at allocation time.
    //
    // Returns:
    //  * ZX_OK:             Successfully freed the memory.
    //  * ZX_ERR_NOT_FOUND:  Could not find a block corresponding to the identifier given.
    zx_status_t FreeSharedMemory(uint64_t mem_id);

    // Attempts to find a previously allocated block of memory.
    //
    // Returns:
    //  * If the block was found, an iterator object pointing to the SharedMemory block.
    //  * Otherwise, an iterator object pointing to the end of allocated_shared_memory_.
    SharedMemoryList::iterator FindSharedMemory(uint64_t mem_id);

    //
    // OP-TEE RPC Function Handlers
    //
    // The section below outlines the functions that are used to parse and fulfill RPC commands from
    // the OP-TEE secure world.
    //
    // There are two main "types" of functions defined and can be identified by their naming
    // convention:
    //  * "HandleRpc" functions handle the first layer of commands. These are basic, fundamental
    //    commands used for critical tasks like setting up shared memory, notifying the normal world
    //    of interrupts, and accessing the second layer of commands.
    //  * "HandleRpcCommand" functions handle the second layer of commands. These are more advanced
    //    commands, like loading trusted applications and accessing the file system. These make up
    //    the bulk of RPC commands once a session is open.
    //      * HandleRpcCommand is actually a specific command in the first layer that can be invoked
    //        once initial shared memory is set up for the command message.
    //
    // Because these RPCs are the primary channel through which the normal and secure worlds mediate
    // shared resources, it is important that handlers in the normal world are resilient to errors
    // from the trusted world. While we don't expect that the trusted world is actively malicious in
    // any way, we do want handlers to be cautious against buggy or unexpected behaviors, as we do
    // not want errors propagating into the normal world (especially with resources like memory).

    // Identifies and dispatches the first layer of RPC command requests.
    zx_status_t HandleRpc(const RpcFunctionArgs& args, RpcFunctionResult* out_result);
    zx_status_t HandleRpcAllocateMemory(const RpcFunctionAllocateMemoryArgs& args,
                                        RpcFunctionAllocateMemoryResult* out_result);
    zx_status_t HandleRpcFreeMemory(const RpcFunctionFreeMemoryArgs& args,
                                    RpcFunctionFreeMemoryResult* out_result);

    // Identifies and dispatches the second layer of RPC command requests.
    //
    // This dispatcher is actually a specific command in the first layer of RPC requests.
    zx_status_t HandleRpcCommand(const RpcFunctionExecuteCommandsArgs& args,
                                 RpcFunctionExecuteCommandsResult* out_result);
    zx_status_t HandleRpcCommandLoadTa(UnmanagedMessage* message);
    zx_status_t HandleRpcCommandReplayMemoryBlock(UnmanagedMessage* message);
    zx_status_t HandleRpcCommandAllocateMemory(UnmanagedMessage* message);
    zx_status_t HandleRpcCommandFreeMemory(UnmanagedMessage* message);

    OpteeController* controller_;
    bool needs_to_close_ = false;
    SharedMemoryList allocated_shared_memory_;
};

} // namespace optee
