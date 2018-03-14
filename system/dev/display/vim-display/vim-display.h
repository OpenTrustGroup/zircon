// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edid.h"
#include <ddk/protocol/gpio.h>

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE  zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

// From uBoot source
#define VFIFO2VD_TO_HDMI_LATENCY 2
#define OSD2_DMC_CAV_INDEX 0x43
#define EDID_BUF_SIZE       256

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    zx_device_t*                        mydevice;
    zx_device_t*                        fbdevice;

    gpio_protocol_t                     gpio;

    thrd_t                              main_thread;

    pdev_vmo_buffer_t                   mmio_preset;
    pdev_vmo_buffer_t                   mmio_hdmitx;
    pdev_vmo_buffer_t                   mmio_hiu;
    pdev_vmo_buffer_t                   mmio_vpu;
    pdev_vmo_buffer_t                   mmio_hdmitx_sec;
    pdev_vmo_buffer_t                   mmio_dmc;
    pdev_vmo_buffer_t                   mmio_cbus;
    pdev_vmo_buffer_t                   fbuffer;
    zx_display_info_t                   disp_info;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    uint8_t*                            edid_buf;
    struct hdmi_param*                  p;
    detailed_timing_t                   std_raw_dtd;
    disp_timing_t                       std_disp_timing;
    disp_timing_t                       pref_disp_timing;


} vim2_display_t;

zx_status_t configure_canvas(vim2_display_t* display);
zx_status_t configure_osd2(vim2_display_t* display);
void osd_debug_dump_register_all(vim2_display_t* display);
void osd_dump(vim2_display_t* display);
zx_status_t get_preferred_res(vim2_display_t* display, uint16_t edid_buf_size);
struct hdmi_param** get_supported_formats(void);