// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-display.h"
#include "hdmitx.h"
#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display-controller.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

/* Default formats */
static const uint8_t _ginput_color_format   = HDMI_COLOR_FORMAT_444;
static const uint8_t _gcolor_depth          = HDMI_COLOR_DEPTH_24B;

static const zx_pixel_format_t _gsupported_pixel_formats = { ZX_PIXEL_FORMAT_RGB_x888 };

typedef struct image_info {
    zx_handle_t pmt;
    uint8_t canvas_idx;

    list_node_t node;
} image_info_t;

// MMIO indices (based on vim2_display_mmios)
enum {
    MMIO_PRESET,
    MMIO_HDMITX,
    MMIO_HIU,
    MMIO_VPU,
    MMIO_HDMTX_SEC,
    MMIO_DMC,
    MMIO_CBUS,
};

static void vim_set_display_controller_cb(void* ctx, void* cb_ctx, display_controller_cb_t* cb) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->cb_lock);

    mtx_lock(&display->display_lock);

    display->dc_cb = cb;
    display->dc_cb_ctx = cb_ctx;

    uint64_t display_id = display->display_id;
    bool attached = display->display_attached;
    mtx_unlock(&display->display_lock);

    if (attached) {
        display->dc_cb->on_displays_changed(display->dc_cb_ctx, &display_id, 1, NULL, 0);
    }
    mtx_unlock(&display->cb_lock);
}

static zx_status_t vim_get_display_info(void* ctx, uint64_t display_id, display_info_t* info) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);
    if (!display->display_attached || display_id != display->display_id) {
        mtx_unlock(&display->display_lock);
        return ZX_ERR_NOT_FOUND;
    }

    info->edid_present = true;
    info->panel.edid.data = display->edid_buf;
    info->panel.edid.length = EDID_BUF_SIZE;
    info->pixel_formats = &_gsupported_pixel_formats;
    info->pixel_format_count = sizeof(_gsupported_pixel_formats) / sizeof(zx_pixel_format_t);

    mtx_unlock(&display->display_lock);
    return ZX_OK;
}

static zx_status_t vim_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo, size_t offset) {
    image_info_t* import_info = calloc(1, sizeof(image_info_t));
    if (import_info == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    unsigned size = ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
    unsigned num_pages = size / PAGE_SIZE;
    zx_paddr_t paddr[num_pages];

    vim2_display_t* display = ctx;
    mtx_lock(&display->image_lock);

    zx_status_t status = zx_bti_pin(display->bti, ZX_BTI_PERM_READ, vmo, offset, size,
                                    paddr, num_pages, &import_info->pmt);
    if (status != ZX_OK) {
        goto fail;
    }

    for (unsigned i = 0; i < num_pages - 1; i++) {
        if (paddr[i] + PAGE_SIZE != paddr[i + 1]) {
            status = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
    }

    if (!add_canvas_entry(display, paddr[0], &import_info->canvas_idx)) {
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }

    list_add_head(&display->imported_images, &import_info->node);
    image->handle = (void*) (uint64_t) import_info->canvas_idx;

    mtx_unlock(&display->image_lock);

    return ZX_OK;
fail:
    mtx_unlock(&display->image_lock);

    if (import_info->pmt != ZX_HANDLE_INVALID) {
        zx_handle_close(import_info->pmt);
    }
    free(import_info);
    return status;
}

static void vim_release_image(void* ctx, image_t* image) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->image_lock);

    image_info_t* info;
    list_for_every_entry(&display->imported_images, info, image_info_t, node) {
        if ((void*) (uint64_t) info->canvas_idx == image->handle) {
            list_delete(&info->node);
            break;
        }
    }

    mtx_unlock(&display->image_lock);

    if (info) {
        free_canvas_entry(display, info->canvas_idx);
        zx_handle_close(info->pmt);
        free(info);
    }
}

static bool vim_check_configuration(void* ctx,
                                    display_config_t** display_configs, uint32_t display_count) {
    if (display_count != 1) {
        return display_count == 0;
    }
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);
    bool res = (display->display_attached
               && display_configs[0]->display_id == display->display_id
               && display_configs[0]->mode.h_addressable == display->width
               && display_configs[0]->image.width == display->width
               && display_configs[0]->mode.v_addressable == display->height
               && display_configs[0]->image.height == display->height);
    mtx_unlock(&display->display_lock);
    return res;
}

static void vim_apply_configuration(void* ctx,
                                    display_config_t** display_configs, uint32_t display_count) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    uint8_t addr;
    if (display_count == 1) {
        // The only way a checked configuration could now be invalid is if display was
        // unplugged. If that's the case, then the upper layers will give a new configuration
        // once they finish handling the unplug event. So just return.
        if (!display->display_attached || display_configs[0]->display_id != display->display_id) {
            mtx_unlock(&display->display_lock);
            return;
        }
        addr = (uint8_t) (uint64_t) display_configs[0]->image.handle;
    } else {
        addr = display->fb_canvas_idx;
    }

    flip_osd2(display, addr);

    mtx_unlock(&display->display_lock);
}

static uint32_t vim_compute_linear_stride(void* ctx, uint32_t width, zx_pixel_format_t format) {
    // The vim2 display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

static zx_status_t allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
    vim2_display_t* display = ctx;
    return zx_vmo_create_contiguous(display->bti, size, 0, vmo_out);
}

static display_controller_protocol_ops_t display_controller_ops = {
    .set_display_controller_cb = vim_set_display_controller_cb,
    .get_display_info = vim_get_display_info,
    .import_vmo_image = vim_import_vmo_image,
    .release_image = vim_release_image,
    .check_configuration = vim_check_configuration,
    .apply_configuration = vim_apply_configuration,
    .compute_linear_stride = vim_compute_linear_stride,
    .allocate_vmo = allocate_vmo,
};

static void display_release(void* ctx) {
    vim2_display_t* display = ctx;

    if (display) {
        zx_interrupt_trigger(display->vsync_interrupt, 0, 0);
        zx_interrupt_trigger(display->inth, 0, 0);
        int res;
        thrd_join(display->vsync_thread, &res);
        thrd_join(display->main_thread, &res);

        gpio_release_interrupt(&display->gpio, 0);

        io_buffer_release(&display->mmio_preset);
        io_buffer_release(&display->mmio_hdmitx);
        io_buffer_release(&display->mmio_hiu);
        io_buffer_release(&display->mmio_vpu);
        io_buffer_release(&display->mmio_hdmitx_sec);
        io_buffer_release(&display->mmio_dmc);
        io_buffer_release(&display->mmio_cbus);
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
        zx_handle_close(display->vsync_interrupt);
        zx_handle_close(display->inth);
        free(display->edid_buf);
        free(display->p);
    }
    free(display);
}

static void display_unbind(void* ctx) {
    vim2_display_t* display = ctx;
    device_remove(display->mydevice);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
    .unbind = display_unbind,
};

static zx_status_t setup_hdmi(vim2_display_t* display)
{
    zx_status_t status;
    // initialize HDMI
    status = init_hdmi_hardware(display);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI hardware initialization failed\n");
        return status;
    }

    status = get_preferred_res(display, EDID_BUF_SIZE);
    if (status != ZX_OK) {
        DISP_ERROR("No display connected!\n");
        return status;

    }

    // allocate frame buffer
    display->format = ZX_PIXEL_FORMAT_RGB_x888;
    display->width  = display->p->timings.hactive;
    display->height = display->p->timings.vactive;
    display->stride = vim_compute_linear_stride(
            display, display->p->timings.hactive, display->format);

    status = io_buffer_init(&display->fbuffer, display->bti,
                            (display->stride * display->height *
                             ZX_PIXEL_FORMAT_BYTES(display->format)),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        return status;
    }


    display->input_color_format = _ginput_color_format;
    display->color_depth = _gcolor_depth;


    status = init_hdmi_interface(display, display->p);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI interface initialization failed\n");
        return status;
    }

    /* Configure Canvas memory */
    add_canvas_entry(display, io_buffer_phys(&display->fbuffer), &display->fb_canvas_idx);

    /* OSD2 setup */
    configure_osd2(display, display->fb_canvas_idx);

    zx_set_framebuffer(get_root_resource(), display->fbuffer.vmo_handle,
                       display->fbuffer.size, display->format,
                       display->width, display->height, display->stride);

    return ZX_OK;
}

static int hdmi_irq_handler(void *arg) {
    vim2_display_t* display = arg;
    zx_status_t status;
    while(1) {
        status = zx_interrupt_wait(display->inth, NULL);
        if (status != ZX_OK) {
            DISP_ERROR("Waiting in Interrupt failed %d\n", status);
            return -1;
        }
        usleep(500000);
        uint8_t hpd;
        status = gpio_read(&display->gpio, 0, &hpd);
        if (status != ZX_OK) {
            DISP_ERROR("gpio_read failed HDMI HPD\n");
            continue;
        }

        mtx_lock(&display->cb_lock);
        mtx_lock(&display->display_lock);

        uint64_t display_added = INVALID_DISPLAY_ID;
        uint64_t display_removed = INVALID_DISPLAY_ID;
        if (hpd && !display->display_attached) {
            DISP_ERROR("Display is connected\n");
            if (setup_hdmi(display) == ZX_OK) {
                display->display_attached = true;
                display_added = display->display_id;
                gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_LOW);
            }
        } else if (!hpd && display->display_attached) {
            DISP_ERROR("Display Disconnected!\n");
            hdmi_shutdown(display);
            free_canvas_entry(display, display->fb_canvas_idx);
            io_buffer_release(&display->fbuffer);

            display_removed = display->display_id;
            display->display_id++;
            display->display_attached = false;

            gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_HIGH);
        }

        mtx_unlock(&display->display_lock);

        if (display->dc_cb &&
                (display_removed != INVALID_DISPLAY_ID || display_added != INVALID_DISPLAY_ID)) {
            display->dc_cb->on_displays_changed(display->dc_cb_ctx,
                                                &display_added,
                                                display_added != INVALID_DISPLAY_ID,
                                                &display_removed,
                                                display_removed != INVALID_DISPLAY_ID);
        }

        mtx_unlock(&display->cb_lock);
    }
}

static int vsync_thread(void *arg)
{
    vim2_display_t* display = arg;

    for (;;) {
        zx_status_t status;
        status = zx_interrupt_wait(display->vsync_interrupt, NULL);
        if (status != ZX_OK) {
            DISP_INFO("Vsync wait failed");
            break;
        }

        mtx_lock(&display->cb_lock);
        mtx_lock(&display->display_lock);

        uint64_t display_id = display->display_id;
        bool attached = display->display_attached;
        zx_paddr_t live = display->current_image;
        mtx_unlock(&display->display_lock);

        if (display->dc_cb && attached) {
            display->dc_cb->on_display_vsync(display->dc_cb_ctx, display_id, (void*) live);
        }

        mtx_unlock(&display->cb_lock);
    }

    return 0;
}

zx_status_t vim2_display_bind(void* ctx, zx_device_t* parent) {
    vim2_display_t* display = calloc(1, sizeof(vim2_display_t));
    if (!display) {
        DISP_ERROR("Could not allocated display structure\n");
        return ZX_ERR_NO_MEMORY;
    }

    display->parent = parent;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &display->pdev);
    if (status !=  ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    status = pdev_get_bti(&display->pdev, 0, &display->bti);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &display->gpio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get Display GPIO protocol\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&display->pdev, MMIO_PRESET, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_preset);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO PRESET\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HDMITX, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HIU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hiu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HIU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_vpu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO VPU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HDMTX_SEC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx_sec);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX SEC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_DMC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dmc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DMC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_CBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_cbus);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO CBUS\n");
        goto fail;
    }

    status = gpio_config(&display->gpio, 0, GPIO_DIR_IN | GPIO_PULL_DOWN);
    if (status != ZX_OK) {
        DISP_ERROR("gpio_config failed for gpio\n");
        goto fail;
    }

    status = gpio_get_interrupt(&display->gpio, 0, ZX_INTERRUPT_MODE_LEVEL_HIGH, &display->inth);
    if (status != ZX_OK) {
        DISP_ERROR("gpio_get_interrupt failed for gpio\n");
        goto fail;
    }

    status = pdev_map_interrupt(&display->pdev, 0, &display->vsync_interrupt);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map vsync interrupt\n");
        goto fail;
    }
    // Enable vsync interupts
    *((uint32_t*)(display->mmio_vpu.virt + VPU_VIU_MISC_CTRL0)) |= (1 << 8);

    // Create EDID Buffer
    display->edid_buf = calloc(1, EDID_BUF_SIZE);
    if (!display->edid_buf) {
        DISP_ERROR("Could not allocated EDID BUf of size %d\n", EDID_BUF_SIZE);
        goto fail;
    }

    display->p = calloc(1, sizeof(struct hdmi_param));
    if (!display->p) {
        DISP_ERROR("Could not allocated hdmi param structure\n");
        goto fail;
    }

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim2-display",
        .ctx = display,
        .ops = &main_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL,
        .proto_ops = &display_controller_ops,
    };

    status = device_add(display->parent, &add_args, &display->mydevice);
    if (status != ZX_OK) {
        DISP_ERROR("Could not add device\n");
        goto fail;
    }

    display->display_id = 1;
    display->display_attached = false;
    list_initialize(&display->imported_images);
    mtx_init(&display->display_lock, mtx_plain);
    mtx_init(&display->image_lock, mtx_plain);
    mtx_init(&display->cb_lock, mtx_plain);

    thrd_create_with_name(&display->main_thread, hdmi_irq_handler, display, "hdmi_irq_handler");
    thrd_create_with_name(&display->vsync_thread, vsync_thread, display, "vsync_thread");

    return ZX_OK;

fail:
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
    return status;

}

static zx_driver_ops_t vim2_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vim2_display_bind,
};

ZIRCON_DRIVER_BEGIN(vim2_display, vim2_display_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VIM_DISPLAY),
ZIRCON_DRIVER_END(vim_2display)
