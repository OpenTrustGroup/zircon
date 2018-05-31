// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <port/port.h>
#include <fcntl.h>
#include <lib/fidl/coding.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "fuchsia/display/c/fidl.h"
#include "vc.h"

static port_handler_t dc_ph;
static int dc_fd;

typedef struct display_info {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    zx_pixel_format_t format;

    uint64_t image_id;

    struct list_node node;
} display_info_t;

static struct list_node display_list = LIST_INITIAL_VALUE(display_list);

static display_info_t* cur_display = nullptr;
static uint8_t fidl_buffer[ZX_CHANNEL_MAX_MSG_BYTES];

// remember whether the virtual console controls the display
bool g_vc_owns_display = false;

void vc_toggle_framebuffer() {
    if (cur_display == nullptr) {
        return;
    }
    fuchsia_display_ControllerSetOwnershipRequest request;
    request.hdr.ordinal = fuchsia_display_ControllerSetOwnershipOrdinal;
    request.active = !g_vc_owns_display;

    zx_status_t status = zx_channel_write(dc_ph.handle, 0, &request, sizeof(request), nullptr, 0);
    if (status != ZX_OK) {
        printf("vc: Failed to toggle ownership %d\n", status);
    }
}

static zx_status_t decode_message(void* bytes, uint32_t num_bytes) {
    fidl_message_header_t* header = (fidl_message_header_t*) bytes;

    if (num_bytes < sizeof(fidl_message_header_t)) {
        printf("vc: Unexpected short message (size=%d)\n", num_bytes);
        return ZX_ERR_INTERNAL;
    }
    zx_status_t res;

    const fidl_type_t* table = nullptr;
    if (header->ordinal == fuchsia_display_ControllerDisplaysChangedOrdinal) {
        table = &fuchsia_display_ControllerDisplaysChangedEventTable;
    } else if (header->ordinal == fuchsia_display_ControllerClientOwnershipChangeOrdinal) {
        table = &fuchsia_display_ControllerClientOwnershipChangeEventTable;
    }
    if (table != nullptr) {
        const char* err;
        if ((res = fidl_decode(table, bytes, num_bytes, nullptr, 0, &err)) != ZX_OK) {
            printf("vc: Error decoding message %d: %s\n", header->ordinal, err);
        }
    } else {
        printf("vc: Error unknown ordinal %d\n", header->ordinal);
        res = ZX_ERR_NOT_SUPPORTED;
    }
    return res;
}

static void handle_ownership_change(fuchsia_display_ControllerClientOwnershipChangeEvent* evt) {
    g_vc_owns_display = evt->has_ownership;

    // If we've gained it, repaint
    if (g_vc_owns_display && g_active_vc) {
        vc_flush_all(g_active_vc);
    }
}

static zx_status_t handle_display_added(fuchsia_display_Info* info, fuchsia_display_Mode* mode,
                                        int32_t pixel_format) {
    fuchsia_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = fuchsia_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.width = mode->horizontal_resolution;
    stride_msg.pixel_format = pixel_format;

    fuchsia_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t status, read_status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &stride_call,
                                  &actual_bytes, &actual_handles, &read_status)) != ZX_OK) {
        printf("vc: Failed to compute fb stride %d %d\n", status, read_status);
        return status == ZX_ERR_CALL_FAILED ? read_status : status;
    }

    if (stride_rsp.stride < mode->horizontal_resolution) {
        printf("vc: Got bad stride\n");
        return ZX_ERR_INVALID_ARGS;
    }

    display_info_t* display_info =
            reinterpret_cast<display_info_t*>(malloc(sizeof(display_info_t)));
    if (!display_info) {
        printf("vc: failed to alloc display info\n");
        return ZX_ERR_NO_MEMORY;
    }

    display_info->id = info->id;
    display_info->width = mode->horizontal_resolution;
    display_info->height = mode->vertical_resolution;
    display_info->stride = stride_rsp.stride;
    display_info->format = reinterpret_cast<int32_t*>(info->pixel_format.data)[0];

    list_add_tail(&display_list, &display_info->node);

    return ZX_OK;
}

static void handle_display_removed(uint64_t id) {
    display_info_t* info = nullptr;
    display_info_t* to_remove = nullptr;
    list_for_every_entry(&display_list, info, display_info_t, node) {
        if (info->id == id) {
            to_remove = info;
        }
    }
    if (to_remove == nullptr) {
        printf("vc: Tried to remove unknown display %ld\n", id);
        return;
    }

    if (to_remove == cur_display) {
        // If we're removing the current display, clean up resources from rebind_display
        set_log_listener_active(false);
        vc_free_gfx();

        cur_display = nullptr;

        fuchsia_display_ControllerReleaseImageRequest release_msg;
        release_msg.hdr.ordinal = fuchsia_display_ControllerReleaseImageOrdinal;
        release_msg.image_id = to_remove->image_id;

        zx_status_t status = zx_channel_write(dc_ph.handle, 0, &release_msg,
                                              sizeof(release_msg), nullptr, 0);
        if (status != ZX_OK) {
            printf("vc: Failed to release image\n");
        }
    }

    list_delete(&to_remove->node);
    free(to_remove);
}

static zx_status_t allocate_vmo(uint32_t size, zx_handle_t* vmo_out) {
    fuchsia_display_ControllerAllocateVmoRequest alloc_msg;
    alloc_msg.hdr.ordinal = fuchsia_display_ControllerAllocateVmoOrdinal;
    alloc_msg.size = size;

    fuchsia_display_ControllerAllocateVmoResponse alloc_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &alloc_msg;
    call_args.rd_bytes = &alloc_rsp;
    call_args.rd_handles = vmo_out;
    call_args.wr_num_bytes = sizeof(alloc_msg);
    call_args.rd_num_bytes = sizeof(alloc_rsp);
    call_args.rd_num_handles = 1;
    uint32_t actual_bytes, actual_handles;
    zx_status_t status, read_status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles, &read_status)) != ZX_OK) {
        printf("vc: Failed to alloc vmo %d %d\n", status, read_status);
        return status == ZX_ERR_CALL_FAILED ? read_status : status;
    }
    if (alloc_rsp.res != ZX_OK) {
        printf("vc: Failed to alloc vmo %d\n", alloc_rsp.res);
        return alloc_rsp.res;
    }
    return actual_handles == 1 ? ZX_OK : ZX_ERR_INTERNAL;
}

static zx_status_t import_vmo(display_info* display, zx_handle_t vmo, uint64_t* id) {
    zx_handle_t vmo_dup;
    zx_status_t status;
    if ((status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup)) != ZX_OK) {
        printf("vc: Failed to dup fb handle %d\n", status);
        return status;
    }

    fuchsia_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_display_ControllerImportVmoImageOrdinal;
    import_msg.image_config.height = display->height;
    import_msg.image_config.width = display->width;
    import_msg.image_config.pixel_format = display->format;
    import_msg.image_config.type = IMAGE_TYPE_SIMPLE;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;

    fuchsia_display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &import_msg;
    call_args.wr_handles = &vmo_dup;
    call_args.rd_bytes = &import_rsp;
    call_args.wr_num_bytes = sizeof(import_msg);
    call_args.wr_num_handles = 1;
    call_args.rd_num_bytes = sizeof(import_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t read_status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles, &read_status)) != ZX_OK) {
        if (status != ZX_ERR_CALL_FAILED) {
            zx_handle_close(vmo_dup);
        }
        printf("vc: Failed to import vmo call %d\n", status);
        return status == ZX_ERR_CALL_FAILED ? read_status : status;
    }

    if (import_rsp.res != ZX_OK) {
        printf("vc: Failed to import vmo %d\n", import_rsp.res);
        return import_rsp.res;
    }

    *id = import_rsp.image_id;
    return ZX_OK;
}

static zx_status_t set_active_image(uint64_t display_id, uint64_t image_id) {
    zx_status_t status;

    // Set our framebuffer as the active framebuffer
    fuchsia_display_ControllerSetDisplayImageRequest set_msg;
    set_msg.hdr.ordinal = fuchsia_display_ControllerSetDisplayImageOrdinal;
    set_msg.display = display_id;
    set_msg.image_id = image_id;
    if ((status = zx_channel_write(dc_ph.handle, 0,
                                   &set_msg, sizeof(set_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Failed to set image %d\n", status);
        return status;
    }

    // Validate and then apply the new configuration
    fuchsia_display_ControllerCheckConfigRequest check_msg;
    fuchsia_display_ControllerCheckConfigResponse check_rsp;
    check_msg.discard = false;
    check_msg.hdr.ordinal = fuchsia_display_ControllerCheckConfigOrdinal;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &check_msg;
    call_args.rd_bytes = &check_rsp;
    call_args.wr_num_bytes = sizeof(check_msg);
    call_args.rd_num_bytes = sizeof(check_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t read_status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles, &read_status)) != ZX_OK) {
        printf("vc: Failed to make validate display config %d\n", status);
        return status == ZX_ERR_CALL_FAILED ? read_status : status;
    }

    if (!check_rsp.valid) {
        printf("vc: Config not valid\n");
        return ZX_ERR_INTERNAL;
    }

    fuchsia_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.ordinal = fuchsia_display_ControllerApplyConfigOrdinal;
    if ((status = zx_channel_write(dc_ph.handle, 0,
                                   &apply_msg, sizeof(apply_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Applying config failed %d\n", status);
        return status;
    }

    return ZX_OK;
}

static zx_status_t rebind_display() {
    if (cur_display != nullptr) {
        return ZX_OK;
    }

    // Pick an arbitrary display
    display_info* display = list_peek_head_type(&display_list, display_info, node);
    if (display == nullptr) {
        printf("vc: No display to bind to\n");
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status;
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    uint64_t image_id = INVALID_ID;

    uint32_t size = display->stride * display->height * ZX_PIXEL_FORMAT_BYTES(display->format);
    if ((status = allocate_vmo(size, &vmo)) != ZX_OK) {
        goto fail;
    }

    if ((status = import_vmo(display, vmo, &image_id)) != ZX_OK) {
        goto fail;
    }

    if ((status = set_active_image(display->id, image_id)) != ZX_OK) {
        goto fail;
    }

    if ((status = vc_init_gfx(vmo, display->width, display->height,
                              display->format, display->stride)) != ZX_OK) {
        printf("vc: failed to initialize graphics for new display %d\n", status);
        goto fail;
    }

    cur_display = display;
    cur_display->image_id = image_id;

    // Only listen for logs when we have somewhere to print them. Also,
    // use a repeating wait so that we don't add/remove observers for each
    // log message (which is helpful when tracing the addition/removal of
    // observers).
    set_log_listener_active(true);
    vc_show_active();

    printf("vc: Successfully attached to display %ld\n", display->id);

    return ZX_OK;

fail:
    if (image_id != INVALID_ID) {
        fuchsia_display_ControllerReleaseImageRequest release_msg;
        release_msg.hdr.ordinal = fuchsia_display_ControllerReleaseImageOrdinal;
        release_msg.image_id = image_id;

        if ((status = zx_channel_write(dc_ph.handle, 0, &release_msg,
                                       sizeof(release_msg), nullptr, 0)) != ZX_OK) {
            printf("vc: Failed to release image\n");
        }
    }
    if (vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(vmo);
    }

    handle_display_removed(display->id);

    return rebind_display();
}

static zx_status_t handle_display_changed(fuchsia_display_ControllerDisplaysChangedEvent* evt) {
    for (unsigned i = 0; i < evt->added.count; i++) {
        fuchsia_display_Info* info = reinterpret_cast<fuchsia_display_Info*>(evt->added.data) + i;
        fuchsia_display_Mode* mode = reinterpret_cast<fuchsia_display_Mode*>(info->modes.data);
        int32_t pixel_format = reinterpret_cast<int32_t*>(info->pixel_format.data)[0];
        zx_status_t status = handle_display_added(info, mode, pixel_format);
        if (status != ZX_OK) {
            return status;
        }
    }

    for (unsigned i = 0; i < evt->removed.count; i++) {
        handle_display_removed(reinterpret_cast<int32_t*>(evt->removed.data)[i]);
    }

    return rebind_display();
}

static zx_status_t dc_callback_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        printf("vc: Displays lost\n");
        while (!list_is_empty(&display_list)) {
            handle_display_removed(list_peek_head_type(&display_list, display_info_t, node)->id);
        }

        close(dc_fd);
        zx_handle_close(dc_ph.handle);
        return ZX_ERR_STOP;
    }
    ZX_DEBUG_ASSERT(signals & ZX_CHANNEL_READABLE);

    zx_status_t status;
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_read(dc_ph.handle, 0,
                                  fidl_buffer, nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Error reading display message %d\n", status);
        return ZX_OK;
    }

    if (decode_message(fidl_buffer, actual_bytes) != ZX_OK) {
        return ZX_OK;
    }

    fidl_message_header_t* header = (fidl_message_header_t*) fidl_buffer;
    switch (header->ordinal) {
    case fuchsia_display_ControllerDisplaysChangedOrdinal: {
        handle_display_changed(
                reinterpret_cast<fuchsia_display_ControllerDisplaysChangedEvent*>(fidl_buffer));
        break;
    }
    case fuchsia_display_ControllerClientOwnershipChangeOrdinal: {
        auto evt = reinterpret_cast<fuchsia_display_ControllerClientOwnershipChangeEvent*>(
                fidl_buffer);
        handle_ownership_change(evt);
        break;
    }
    default:
        printf("vc: Unknown display callback message %d\n", header->ordinal);
        break;
    }

    return ZX_OK;
}

bool vc_display_init() {
    for (;;) {
        if ((dc_fd= open("/dev/class/display-controller/000/virtcon", O_RDWR)) >= 0) {
            break;
        }
        usleep(100000);
    }

    if (ioctl_display_controller_get_handle(dc_fd, &dc_ph.handle) != sizeof(zx_handle_t)) {
        printf("vc: failed to get display controller handle\n");
        return false;
    }

    zx_status_t status;
    dc_ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    dc_ph.func = dc_callback_handler;
    if ((status = port_wait(&port, &dc_ph)) != ZX_OK) {
        printf("vc: Failed to set port waiter %d\n", status);
        return false;
    }

    return true;
}
