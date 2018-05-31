// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "vc.h"

gfx_surface* vc_gfx;
gfx_surface* vc_tb_gfx;

const gfx_font* vc_font;

void vc_gfx_draw_char(vc_t* vc, vc_char_t ch, unsigned x, unsigned y,
                      bool invert) {
    uint8_t fg_color = vc_char_get_fg_color(ch);
    uint8_t bg_color = vc_char_get_bg_color(ch);
    if (invert) {
        // Swap the colors.
        uint8_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }
    gfx_putchar(vc_gfx, vc->font, vc_char_get_char(ch),
                x * vc->charw, y * vc->charh,
                palette_to_color(vc, fg_color),
                palette_to_color(vc, bg_color));
}

#if BUILD_FOR_TEST
static gfx_surface* vc_test_gfx;

zx_status_t vc_init_gfx(gfx_surface* test) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    vc_test_gfx = test;

    // init the status bar
    vc_tb_gfx = gfx_create_surface(NULL, test->width, font->height,
                                   test->stride, test->format, 0);
    if (!vc_tb_gfx) {
        return ZX_ERR_NO_MEMORY;
    }

    // init the main surface
    vc_gfx = gfx_create_surface(NULL, test->width, test->height,
                                test->stride, test->format, 0);
    if (!vc_gfx) {
        gfx_surface_destroy(vc_tb_gfx);
        vc_tb_gfx = NULL;
        return ZX_ERR_NO_MEMORY;
    }

    g_status_width = vc_gfx->width / font->width;

    return ZX_OK;
}

void vc_gfx_invalidate_all(vc_t* vc) {
    gfx_copylines(vc_test_gfx, vc_tb_gfx, 0, 0, vc_tb_gfx->height);
    gfx_copylines(vc_test_gfx, vc_gfx, 0, vc_tb_gfx->height, vc_gfx->height - vc_tb_gfx->height);
}

void vc_gfx_invalidate_status() {
    gfx_copylines(vc_test_gfx, vc_tb_gfx, 0, 0, vc_tb_gfx->height);
}

void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc_tb_gfx->height + y * vc->charh;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc_test_gfx, vc_gfx, y * vc->charh, desty, h * vc->charh);
    } else {
        gfx_blend(vc_test_gfx, vc_gfx, x * vc->charw, y * vc->charh,
                  w * vc->charw, h * vc->charh, x * vc->charw, desty);
    }
}

void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc_tb_gfx->height + y;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc_test_gfx, vc_gfx, y, desty, h);
    } else {
        gfx_blend(vc_test_gfx, vc_gfx, x, y, w, h, x, desty);
    }
}
#else
static zx_handle_t vc_gfx_vmo = ZX_HANDLE_INVALID;
static uintptr_t vc_gfx_mem = 0;
static size_t vc_gfx_size = 0;

void vc_free_gfx() {
    if (vc_gfx) {
        gfx_surface_destroy(vc_gfx);
        vc_gfx = NULL;
    }
    if (vc_tb_gfx) {
        gfx_surface_destroy(vc_tb_gfx);
        vc_tb_gfx = NULL;
    }
    if (vc_gfx_mem) {
        zx_vmar_unmap(zx_vmar_root_self(), vc_gfx_mem, vc_gfx_size);
        vc_gfx_mem = 0;
    }
    if (vc_gfx_vmo) {
        zx_handle_close(vc_gfx_vmo);
        vc_gfx_vmo = ZX_HANDLE_INVALID;
    }
}

zx_status_t vc_init_gfx(zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    uintptr_t ptr;
    vc_gfx_vmo = fb_vmo;
    vc_gfx_size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;

    zx_status_t r;
    if ((r = zx_vmar_map(zx_vmar_root_self(), 0, vc_gfx_vmo, 0, vc_gfx_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &vc_gfx_mem)) < 0) {
        goto fail;
    }

    r = ZX_ERR_NO_MEMORY;
    // init the status bar
    if ((vc_tb_gfx = gfx_create_surface((void*) vc_gfx_mem, width, font->height,
                                        stride, format, 0)) == NULL) {
        goto fail;
    }

    // init the main surface
    ptr = vc_gfx_mem + stride * font->height * ZX_PIXEL_FORMAT_BYTES(format);
    if ((vc_gfx = gfx_create_surface((void*) ptr, width, height - font->height,
                                     stride, format, 0)) == NULL) {
        goto fail;
    }

    g_status_width = vc_gfx->width / font->width;

    return ZX_OK;

fail:
    vc_free_gfx();
    return r;
}

void vc_gfx_invalidate_all(vc_t* vc) {
    if (vc->active) {
        zx_cache_flush(reinterpret_cast<void*>(vc_gfx_mem), vc_gfx_size, ZX_CACHE_FLUSH_DATA);
    }
}

void vc_gfx_invalidate_status() {
    zx_cache_flush(reinterpret_cast<void*>(vc_gfx_mem),
                   vc_tb_gfx->stride * vc_tb_gfx->height * vc_tb_gfx->pixelsize,
                   ZX_CACHE_FLUSH_DATA);
}

// pixel coords
void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!vc->active) {
        return;
    }
    uint32_t flush_size = w * vc_gfx->pixelsize;
    uintptr_t addr = vc_gfx_mem + (vc_gfx->stride * (vc->charh + y) * vc_gfx->pixelsize);
    for (unsigned i = 0; i < h; i++, addr += (vc_gfx->stride * vc_gfx->pixelsize)) {
        zx_cache_flush(reinterpret_cast<void*>(addr), flush_size, ZX_CACHE_FLUSH_DATA);
    }
}

// text coords
void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    vc_gfx_invalidate_region(vc, x * vc->charw, y * vc->charh, w * vc->charw, h * vc->charh);
}
#endif
