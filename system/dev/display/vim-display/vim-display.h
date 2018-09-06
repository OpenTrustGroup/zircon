// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "edid.h"
#include <assert.h>
#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/amlogic-canvas.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/display-controller.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/listnode.h>
#include <zircon/pixelformat.h>

#include "vim-audio.h"

__BEGIN_CDECLS

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE  zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define NUM_CANVAS_ENTRIES 256
#define CANVAS_BYTE_STRIDE 32

// From uBoot source
#define VFIFO2VD_TO_HDMI_LATENCY 2
#define EDID_BUF_SIZE       256

// MMIO indices (based on vim2_display_mmios)
enum {
    MMIO_PRESET = 0,
    MMIO_HDMITX,
    MMIO_HIU,
    MMIO_VPU,
    MMIO_HDMTX_SEC,
    MMIO_DMC,
    MMIO_CBUS,
    MMIO_AUD_OUT,
    MMIO_COUNT  // Must be the final entry
};

// BTI indices (based on vim2_display_btis)
enum {
    BTI_DISPLAY = 0,
    BTI_AUDIO,
    BTI_COUNT  // Must be the final entry
};

typedef struct vim2_display {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    zx_device_t*                        mydevice;
    zx_handle_t                         bti;
    zx_handle_t                         inth;

    gpio_protocol_t                     gpio;
    canvas_protocol_t                   canvas;

    thrd_t                              main_thread;
    thrd_t                              vsync_thread;
    // Lock for general display state, in particular display_id.
    mtx_t                               display_lock;
    // Lock for imported images.
    mtx_t                               image_lock;

    // TODO(stevensd): This can race if this is changed right after
    // vsync but before the interrupt is handled.
    bool                                current_image_valid;
    uint8_t                             current_image;
    bool                                vd1_image_valid;
    uint32_t                            vd1_image;

    io_buffer_t                         mmio_preset;
    io_buffer_t                         mmio_hdmitx;
    io_buffer_t                         mmio_hiu;
    io_buffer_t                         mmio_vpu;
    io_buffer_t                         mmio_hdmitx_sec;
    io_buffer_t                         mmio_dmc;
    io_buffer_t                         mmio_cbus;

    zx_handle_t                         vsync_interrupt;

    bool                                display_attached;
    // The current display id (if display_attached), or the next display id
    uint64_t                            display_id;
    uint32_t                            width;
    uint32_t                            height;
    uint32_t                            stride;
    zx_pixel_format_t                   format;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    uint8_t*                            edid_buf;
    uint16_t                            edid_length;
    struct hdmi_param*                  p;
    detailed_timing_t                   std_raw_dtd;
    disp_timing_t                       std_disp_timing;
    disp_timing_t                       pref_disp_timing;

    display_controller_cb_t*            dc_cb;
    void*                               dc_cb_ctx;
    list_node_t                         imported_images;

    // A reference to the object which controls the VIM2 DAIs used to feed audio
    // into the HDMI stream.
    vim2_audio_t*                       audio;
} vim2_display_t;

void disable_vd(vim2_display_t* display, uint32_t vd_index);
void configure_vd(vim2_display_t* display, uint32_t vd_index);
void flip_vd(vim2_display_t* display, uint32_t vd_index, uint32_t index);

void disable_osd(vim2_display_t* display, uint32_t osd_index);
zx_status_t configure_osd(vim2_display_t* display, uint32_t osd_index);
void flip_osd(vim2_display_t* display, uint32_t osd_index, uint8_t idx);
void osd_debug_dump_register_all(vim2_display_t* display);
void osd_dump(vim2_display_t* display);
zx_status_t get_preferred_res(vim2_display_t* display, uint16_t edid_buf_size);
struct hdmi_param** get_supported_formats(void);

// TODO(johngro) : eliminate the need for these hooks if/when we start to
// support composite device drivers and can separate the DAI driver from the
// HDMI driver (which is currently playing the role of codec driver)
//
// TODO(johngro) : add any info needed to properly set up the audio info-frame.
zx_status_t vim2_display_configure_audio_mode(const vim2_display_t* display,
                                              uint32_t N,
                                              uint32_t CTS,
                                              uint32_t frame_rate,
                                              uint32_t bits_per_sample);
void vim2_display_disable_audio(const vim2_display_t* display);

__END_CDECLS
