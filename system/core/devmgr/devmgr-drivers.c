// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "devmgr.h"
#include "devcoordinator.h"
#include "log.h"

#include <driver-info/driver-info.h>

#include <zircon/driver/binding.h>

typedef struct {
    const char* libname;
    void (*func)(driver_t* drv, const char* version);
} add_ctx_t;

static bool is_driver_disabled(const char* name) {
    // driver.<driver_name>.disable
    char opt[16 + DRIVER_NAME_LEN_MAX];
    snprintf(opt, 16 + DRIVER_NAME_LEN_MAX, "driver.%s.disable", name);
    return getenv_bool(opt, false);
}

static void found_driver(zircon_driver_note_payload_t* note,
                         const zx_bind_inst_t* bi, void* cookie) {
    add_ctx_t* ctx = cookie;

    // ensure strings are terminated
    note->name[sizeof(note->name) - 1] = 0;
    note->vendor[sizeof(note->vendor) - 1] = 0;
    note->version[sizeof(note->version) - 1] = 0;

    if (is_driver_disabled(note->name)) {
        return;
    }

    const char* libname = ctx->libname;
    size_t pathlen = strlen(libname) + 1;
    size_t namelen = strlen(note->name) + 1;
    size_t bindlen = note->bindcount * sizeof(zx_bind_inst_t);
    size_t len = sizeof(driver_t) + bindlen + pathlen + namelen;

    if ((note->flags & ZIRCON_DRIVER_NOTE_FLAG_ASAN) && !dc_asan_drivers) {
        if (dc_launched_first_devhost) {
            log(ERROR, "%s (%s) requires ASan: cannot load after boot;"
                " consider devmgr.devhost.asan=true\n",
                libname, note->name);
            return;
        }
        dc_asan_drivers = true;
    }

    driver_t* drv;
    if ((drv = malloc(len)) == NULL) {
        return;
    }

    memset(drv, 0, sizeof(driver_t));
    drv->binding_size = bindlen;
    drv->binding = (void*) (drv + 1);
    drv->libname = (void*) (drv->binding + note->bindcount);
    drv->name = drv->libname + pathlen;

    memcpy((void*) drv->binding, bi, bindlen);
    memcpy((void*) drv->libname, libname, pathlen);
    memcpy((void*) drv->name, note->name, namelen);

#if VERBOSE_DRIVER_LOAD
    printf("found driver: %s\n", (char*) cookie);
    printf("        name: %s\n", note->name);
    printf("      vendor: %s\n", note->vendor);
    printf("     version: %s\n", note->version);
    printf("       flags: %#x\n", note->flags);
    printf("     binding:\n");
    for (size_t n = 0; n < note->bindcount; n++) {
        printf("         %03zd: %08x %08x\n", n, bi[n].op, bi[n].arg);
    }
#endif

    ctx->func(drv, note->version);
}

void find_loadable_drivers(const char* path,
                           void (*func)(driver_t* drv, const char* version)) {

    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') {
            continue;
        }
        if (de->d_type != DT_REG) {
            continue;
        }
        char libname[256 + 32];
        int r = snprintf(libname, sizeof(libname), "%s/%s", path, de->d_name);
        if ((r < 0) || (r >= (int)sizeof(libname))) {
            continue;
        }

        int fd;
        if ((fd = openat(dirfd(dir), de->d_name, O_RDONLY)) < 0) {
            continue;
        }
        add_ctx_t ctx = {
            .libname = libname,
            .func = func,
        };
        zx_status_t status = di_read_driver_info(fd, &ctx, found_driver);
        close(fd);

        if (status) {
            if (status == ZX_ERR_NOT_FOUND) {
                printf("devcoord: no driver info in '%s'\n", libname);
            } else {
                printf("devcoord: error reading info from '%s'\n", libname);
            }
        }
    }
    closedir(dir);
}

void load_driver(const char* path,
                 void (*func)(driver_t* drv, const char* version)) {
    //TODO: check for duplicate driver add
    int fd;
    if ((fd = open(path, O_RDONLY)) < 0) {
        printf("devcoord: cannot open '%s'\n", path);
        return;
    }

    add_ctx_t ctx = {
        .libname = path,
        .func = func,
    };
    zx_status_t status = di_read_driver_info(fd, &ctx, found_driver);
    close(fd);

    if (status) {
        if (status == ZX_ERR_NOT_FOUND) {
            printf("devcoord: no driver info in '%s'\n", path);
        } else {
            printf("devcoord: error reading info from '%s'\n", path);
        }
    }
}
