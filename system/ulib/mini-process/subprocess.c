// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "subprocess.h"
#include <mini-process/mini-process.h>

// This function is the entire program that the child process will execute. It
// gets directly mapped into the child process via zx_vmo_write() so it must not
// reference any addressable entity outside it.
void minipr_thread_loop(zx_handle_t channel, uintptr_t fnptr) {
    if (fnptr == 0) {
        // In this mode we don't have a VDSO so we don't care what the handle is
        // and therefore we busy-loop. Unless external steps are taken this will
        // saturate one core.
        volatile uint32_t val = 1;
        while (val) {
            val += 2u;
        }
    } else {
        // In this mode we do have a VDSO but we are not a real ELF program so
        // we need to receive from the parent the address of the syscalls we can
        // use. So we can bootstrap, kernel has already transferred the address of
        // zx_channel_read() and the handle to one end of the channel which already
        // contains a message with the rest of the syscall addresses.
        __typeof(zx_channel_read)* read_fn = (__typeof(zx_channel_read)*)fnptr;

        uint32_t actual = 0u;
        uint32_t actual_handles = 0u;
        zx_handle_t handle[2];
        minip_ctx_t ctx;

        zx_status_t status = (*read_fn)(
                channel, 0u, &ctx, handle, sizeof(ctx), 1, &actual, &actual_handles);
        if ((status != ZX_OK) || (actual != sizeof(ctx)))
            __builtin_trap();

        // The received handle in the |ctx| message does not have any use other than
        // keeping it alive until the process ends. We basically leak it.

        // Acknowledge the initial message.
        uint32_t ack[2] = { actual, actual_handles };
        status = ctx.channel_write(channel, 0u, ack, sizeof(uint32_t) * 2, NULL, 0u);
        if (status != ZX_OK)
            __builtin_trap();

        do {
            // wait for the next message.
            status = ctx.object_wait_one(
                channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, &actual);
            if (status != ZX_OK)
                break;

            minip_cmd_t cmd;
            status = ctx.channel_read(
                channel, 0u, &cmd, NULL,  sizeof(cmd), 0u, &actual, &actual_handles);

            // Execute one or more commands. After each we send a reply with the
            // result. If the command does not cause to crash or exit.
            uint32_t what = cmd.what;

            do {
                // This loop is convoluted. A simpler switch() statement
                // has the risk of being generated as a table lookup which
                // makes it likely it will reference the data section which
                // is outside the memory copied to the child.

                handle[0] = ZX_HANDLE_INVALID;
                handle[1] = ZX_HANDLE_INVALID;

                if (what & MINIP_CMD_ECHO_MSG) {
                    what &= ~MINIP_CMD_ECHO_MSG;
                    cmd.status = ZX_OK;
                    goto reply;
                }
                if (what & MINIP_CMD_CREATE_EVENT) {
                    what &= ~MINIP_CMD_CREATE_EVENT;
                    cmd.status = ctx.event_create(0u, &handle[0]);
                    goto reply;
                }
                if (what & MINIP_CMD_CREATE_CHANNEL) {
                    what &= ~MINIP_CMD_CREATE_CHANNEL;
                    cmd.status = ctx.channel_create(0u, &handle[0], &handle[1]);
                    goto reply;
                }
                if (what & MINIP_CMD_USE_BAD_HANDLE_CLOSED) {
                    what &= ~MINIP_CMD_USE_BAD_HANDLE_CLOSED;

                    // Test one case of using an invalid handle.  This
                    // tests a double-close of an event handle.
                    zx_handle_t handle;
                    if (ctx.event_create(0u, &handle) != ZX_OK ||
                        ctx.handle_close(handle) != ZX_OK)
                        __builtin_trap();
                    cmd.status = ctx.handle_close(handle);
                    goto reply;
                }
                if (what & MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED) {
                    what &= ~MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED;

                    // Test another case of using an invalid handle.  This
                    // tests closing a handle after it has been transferred
                    // out of the process (by writing it to a channel).  In
                    // this case, the Handle object still exists inside the
                    // kernel.
                    zx_handle_t handle;
                    zx_handle_t channel1;
                    zx_handle_t channel2;
                    if (ctx.event_create(0u, &handle) != ZX_OK ||
                        ctx.channel_create(0u, &channel1, &channel2) != ZX_OK ||
                        ctx.channel_write(channel1, 0, NULL, 0,
                                          &handle, 1) != ZX_OK)
                        __builtin_trap();
                    // This should produce an error and/or exception.
                    cmd.status = ctx.handle_close(handle);
                    // Clean up.
                    if (ctx.handle_close(channel1) != ZX_OK ||
                        ctx.handle_close(channel2) != ZX_OK)
                        __builtin_trap();
                    goto reply;
                }
                if (what & MINIP_CMD_VALIDATE_CLOSED_HANDLE) {
                    what &= ~MINIP_CMD_VALIDATE_CLOSED_HANDLE;

                    zx_handle_t event;
                    if (ctx.event_create(0u, &event) != ZX_OK)
                        __builtin_trap();
                    ctx.handle_close(event);
                    cmd.status = ctx.object_get_info(
                        event, ZX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL);
                    goto reply;
                }

                // Neither MINIP_CMD_BUILTIN_TRAP nor MINIP_CMD_EXIT_NORMAL send a
                // message so the client will get ZX_CHANNEL_PEER_CLOSED.

                if (what & MINIP_CMD_BUILTIN_TRAP)
                    __builtin_trap();

                if (what & MINIP_CMD_EXIT_NORMAL)
                    ctx.process_exit(0);

                // Did not match any known message.
                cmd.status = ZX_ERR_WRONG_TYPE;
reply:
                actual_handles = (handle[0] == ZX_HANDLE_INVALID) ? 0u : 1u;
                status = ctx.channel_write(
                    channel, 0u, &cmd, sizeof(cmd), handle, actual_handles);

                // Loop if there are more commands packed in |what|.
            } while (what);

        } while (status == ZX_OK);
    }

    __builtin_trap();
}
