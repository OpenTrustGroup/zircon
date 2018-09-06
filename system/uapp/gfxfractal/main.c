// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <gfx/gfx.h>

#include <lib/framebuffer/framebuffer.h>

int main(int argc, char* argv[]) {
    const char* err;
    zx_status_t status = fb_bind(true, &err);
    if (status != ZX_OK) {
        printf("failed to open framebuffer: %d (%s)\n", status, err);
        return -1;
    }
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    zx_pixel_format_t format;

    fb_get_config(&width, &height, &stride, &format);

    size_t size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;
    uintptr_t fbo;
    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         0, fb_get_single_buffer(), 0, size, &fbo);
    if (status < 0) {
        printf("failed to map fb (%d)\n", status);
        return -1;
    }

    gfx_surface* gfx = gfx_create_surface((void*)fbo, width, height, stride,
                                          format, GFX_FLAG_FLUSH_CPU_CACHE);
    if (!gfx) {
        printf("failed to create gfx surface\n");
        return -1;
    }
    gfx_fillrect(gfx, 0, 0, gfx->width, gfx->height, 0xffffffff);
    gfx_flush(gfx);

    double a,b, dx, dy, mag, c, ci;
    uint32_t color,iter,x,y;

    bool rotate = (gfx->height > gfx->width);

    dx= 3.0/((double)gfx->width);
    dy= 3.0/((double)gfx->height);
    c = -2.0;
    ci = -1.5;
    for (y = 0; y < gfx->height; y++) {
        if (rotate) {
            ci = -1.5;
        } else {
            c = -2.0;
        }
        for (x = 0; x < gfx->width; x++) {
            a=0;
            b=0;
            mag=0;
            iter = 0;
            while ((mag < 4.0) && (iter < 200) ){
                double a1;
                a1 = a*a - b*b + c;
                b = 2.0 * a * b + ci;
                a=a1;
                mag = a*a + b*b;
                iter++;
            }
            if (rotate) {
                ci = ci + dx;
            } else {
                c = c + dx;
            }
            if (iter == 200) {
                color = 0;
            } else {
                color = 0x231AF9 * iter;
            }
            color= color | 0xff000000;
            gfx_putpixel(gfx, x, y, color);

        }
        if ((y%50) == 0)
            gfx_flush(gfx);
        if (rotate) {
            c = c + dy;
        } else {
            ci = ci + dy;
        }
    }
    gfx_flush(gfx);
    zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

    gfx_surface_destroy(gfx);
    fb_release();
}
