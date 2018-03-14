// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <loader-service/loader-service.h>
#include <zircon/boot/bootdata.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fdio/namespace.h>
#include <fdio/util.h>

#include "devmgr.h"
#include "memfs-private.h"

// Global flag tracking if devmgr believes this is a full Fuchsia build
// (requiring /system, etc) or not.
bool require_system;

// The handle used to transmit messages to appmgr.
static zx_handle_t svc_root_handle;
// The handle used by appmgr to serve incoming requests.
// If appmgr cannot be launched within a timeout, this handle is closed.
static zx_handle_t svc_request_handle;

bool getenv_bool(const char* key, bool _default) {
    const char* value = getenv(key);
    if (value == NULL) {
        return _default;
    }
    if ((strcmp(value, "0") == 0) ||
        (strcmp(value, "false") == 0) ||
        (strcmp(value, "off") == 0)) {
        return false;
    }
    return true;
}

static zx_handle_t root_resource_handle;
static zx_handle_t root_job_handle;
static zx_handle_t svcs_job_handle;
static zx_handle_t fuchsia_job_handle;

zx_handle_t virtcon_open;

zx_handle_t get_root_resource(void) {
    return root_resource_handle;
}

zx_handle_t get_sysinfo_job_root(void) {
    zx_handle_t h;
    //TODO: limit to enumerate rights
    if (zx_handle_duplicate(root_job_handle, ZX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return ZX_HANDLE_INVALID;
    } else {
        return h;
    }
}

static const char* argv_sh[] = { "/boot/bin/sh" };
static const char* argv_appmgr[] = { "/system/bin/appmgr" };

void do_autorun(const char* name, const char* env) {
    char* cmd = getenv(env);
    if (!cmd) {
        return;
    }

    // Get the full commandline by splitting on '+'.
    char* buf = strdup(cmd);
    if (buf == NULL) {
        printf("devmgr: %s: Can't parse %s\n", env, cmd);
        return;
    }
    const int MAXARGS = 8;
    char* argv[MAXARGS];
    int argc = 0;
    char* token;
    char* rest = buf;
    while (argc < MAXARGS && (token = strtok_r(rest, "+", &rest))) {
        argv[argc++] = token;
    }
    printf("devmgr: %s: starting", env);
    for (int i = 0; i < argc; i++) {
        printf(" '%s'", argv[i]);
    }
    printf("...\n");
    devmgr_launch(svcs_job_handle, name,
                  argc, (const char* const*)argv,
                  NULL, -1, NULL, NULL, 0, NULL, FS_ALL);
    free(buf);
}

static zx_handle_t fshost_event;

static int fuchsia_starter(void* arg) {
    bool appmgr_started = false;
    bool autorun_started = false;
    bool drivers_loaded = false;

    zx_time_t deadline = zx_deadline_after(ZX_SEC(10));

    do {
        zx_status_t status = zx_object_wait_one(fshost_event, FSHOST_SIGNAL_READY, deadline, NULL);
        if (status == ZX_ERR_TIMED_OUT) {
            if (svc_request_handle != ZX_HANDLE_INVALID) {
                if (require_system) {
                    printf("devmgr: appmgr not launched in 10s, closing svc handle\n");
                }
                zx_handle_close(svc_request_handle);
            }
            deadline = ZX_TIME_INFINITE;
            continue;
        }
        if (status != ZX_OK) {
            printf("devmgr: error waiting on fuchsia start event: %d\n", status);
            break;
        }
        zx_object_signal(fshost_event, FSHOST_SIGNAL_READY, 0);

        if (!drivers_loaded) {
            // we're starting the appmgr because /system is present
            // so we also signal the device coordinator that those
            // drivers are now loadable
            load_system_drivers();
            drivers_loaded = true;
        }

        struct stat s;
        if (!appmgr_started && stat(argv_appmgr[0], &s) == 0) {
            unsigned int appmgr_hnd_count = 0;
            zx_handle_t appmgr_hnds[2] = {};
            uint32_t appmgr_ids[2] = {};
            if (svc_request_handle) {
                assert(appmgr_hnd_count < countof(appmgr_hnds));
                appmgr_hnds[appmgr_hnd_count] = svc_request_handle;
                appmgr_ids[appmgr_hnd_count] = PA_DIRECTORY_REQUEST;
                appmgr_hnd_count++;
                svc_request_handle = ZX_HANDLE_INVALID;
            }
            devmgr_launch(fuchsia_job_handle, "appmgr", countof(argv_appmgr),
                          argv_appmgr, NULL, -1, appmgr_hnds, appmgr_ids,
                          appmgr_hnd_count, NULL, FS_FOR_APPMGR);
            appmgr_started = true;
        }
        if (!autorun_started) {
            do_autorun("autorun:system", "zircon.autorun.system");
            autorun_started = true;
        }
    } while (!appmgr_started);
    return 0;
}

int service_starter(void* arg) {
    // Features like Intel Processor Trace need a dump of ld.so activity.
    // The output has a specific format, and will eventually be recorded
    // via a specific mechanism (magenta tracing support), so we use a specific
    // env var (and don't, for example, piggyback on LD_DEBUG).
    // We enable this pretty early so that we get a trace of as many processes
    // as possible.
    if (getenv(LDSO_TRACE_CMDLINE)) {
        // This takes care of places that clone our environment.
        putenv(strdup(LDSO_TRACE_ENV));
        // There is still devmgr_launch() which does not clone our enviroment.
        // It has its own check.
    }

    // Start crashlogger.
    if (!getenv_bool("crashlogger.disable", false)) {
        static const char* argv_crashlogger[] = {
            "/boot/bin/crashlogger",
            NULL,  // room for -pton
        };
        const char* crashlogger_pt = getenv("crashlogger.pt");
        int argc_crashlogger = 1;
        if (crashlogger_pt && strcmp(crashlogger_pt, "true") == 0) {
            // /dev/misc/intel-pt may not be available yet, so we can't
            // actually turn on PT here. Just tell crashlogger to dump the
            // trace buffers if they're available.
            argv_crashlogger[argc_crashlogger++] = "-pton";
        }

        // Bind the exception port now, to avoid missing any crashes that
        // might occur early on before the crashlogger process has finished
        // initializing.
        zx_handle_t exception_port;
        // This should match the value used by crashlogger.
        const uint64_t kSysExceptionKey = 1166444u;
        if (zx_port_create(0, &exception_port) == ZX_OK &&
            zx_task_bind_exception_port(ZX_HANDLE_INVALID, exception_port,
                                        kSysExceptionKey, 0) == ZX_OK) {
            zx_handle_t handles[] = { exception_port };
            uint32_t handle_types[] = { PA_HND(PA_USER0, 0) };

            devmgr_launch(svcs_job_handle, "crashlogger",
                          argc_crashlogger, argv_crashlogger,
                          NULL, -1, handles, handle_types,
                          countof(handles), NULL, 0);
        }
    }

    char vcmd[64];
    __UNUSED bool netboot = false;
    bool vruncmd = false;
    if (!getenv_bool("netsvc.disable", false)) {
        const char* args[] = { "/boot/bin/netsvc", NULL, NULL, NULL, NULL };
        int argc = 1;

        if (getenv_bool("netsvc.netboot", false)) {
            args[argc++] = "--netboot";
            netboot = true;
            vruncmd = true;
        }

        const char* interface;
        if ((interface = getenv("netsvc.interface")) != NULL) {
            args[argc++] = "--interface";
            args[argc++] = interface;
        }

        const char* nodename = getenv("zircon.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        zx_handle_t proc;
        if (devmgr_launch(svcs_job_handle, "netsvc", argc, args,
                          NULL, -1, NULL, NULL, 0, &proc, FS_ALL) == ZX_OK) {
            if (vruncmd) {
                zx_info_handle_basic_t info = {
                    .koid = 0,
                };
                zx_object_get_info(proc, ZX_INFO_HANDLE_BASIC,
                                   &info, sizeof(info), NULL, NULL);
                zx_handle_close(proc);
                snprintf(vcmd, sizeof(vcmd), "dlog -f -t -p %zu", info.koid);
            }
        } else {
            vruncmd = false;
        }
    }

    if (!getenv_bool("virtcon.disable", false)) {
        // pass virtcon.* options along
        const char* envp[16];
        unsigned envc = 0;
        char** e = environ;
        while (*e && (envc < countof(envp))) {
            if (!strncmp(*e, "virtcon.", 8)) {
                envp[envc++] = *e;
            }
            e++;
        }
        envp[envc] = NULL;

        uint32_t type = PA_HND(PA_USER0, 0);
        zx_handle_t h = ZX_HANDLE_INVALID;
        zx_channel_create(0, &h, &virtcon_open);
        const char* args[] = { "/boot/bin/virtual-console", "--run", vcmd };
        devmgr_launch(svcs_job_handle, "virtual-console",
                      vruncmd ? 3 : 1, args, envp, -1,
                      &h, &type, (h == ZX_HANDLE_INVALID) ? 0 : 1, NULL, FS_ALL);
    }

    const char* epoch = getenv("devmgr.epoch");
    if (epoch) {
        zx_time_t offset = ZX_SEC(atoi(epoch));
        zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
    }

    do_autorun("autorun:boot", "zircon.autorun.boot");

    thrd_t t;
    if ((thrd_create_with_name(&t, fuchsia_starter, NULL, "fuchsia-starter")) == thrd_success) {
        thrd_detach(t);
    }

    return 0;
}

static int console_starter(void* arg) {
    // if no kernel shell on serial uart, start a sh there
    printf("devmgr: shell startup\n");

    // If we got a TERM environment variable (aka a TERM=... argument on
    // the kernel command line), pass this down.
    const char* term = getenv("TERM");
    if (term != NULL)
        term -= sizeof("TERM=") - 1;

    const char* device = getenv("console.path");
    if (!device)
        device = "/dev/misc/console";

    const char* envp[] = { term ? term : NULL, NULL, };
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open(device, O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          countof(argv_sh), argv_sh, envp, fd, NULL, NULL, 0, NULL, FS_ALL);
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    }
    return 0;
}

static void start_console_shell(void) {
    // start a shell on the kernel console if it isn't already running a shell
    if (!getenv_bool("kernel.shell", false)) {
        thrd_t t;
        if ((thrd_create_with_name(&t, console_starter, NULL, "console-starter")) == thrd_success) {
            thrd_detach(t);
        }
    }
}

static void load_cmdline_from_bootfs(void) {
    int fd = open("/boot/config/devmgr", O_RDONLY);
    if (fd < 0) {
        return;
    }
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char* cfg;
    if ((sz < 0) || ((cfg = malloc(sz + 1)) == NULL)) {
        close(fd);
        return;
    }
    char* x = cfg;
    while (sz > 0) {
        int r = read(fd, x, sz);
        if (r <= 0) {
            close(fd);
            free(cfg);
            return;
        }
        x += r;
        sz -= r;
    }
    *x = 0;
    close(fd);

    x = cfg;
    while (*x) {
        // skip any leading whitespace
        while (isspace(*x)) {
            x++;
        }

        // find the next line (seek for CR or NL)
        char* next = x;
        for (;;) {
            // eof? we're all done then
            if (*next == 0) {
                return;
            }
            if ((*next == '\r') || (*next == '\n')) {
                *next++ = 0;
                break;
            }
            next++;
        }

        // process line if not a comment and not a zero-length name
        if ((*x != '#') && (*x != '=')) {
            for (char *y = x; *y != 0; y++) {
                // space in name is invalid, give up
                if (isspace(*y)) {
                    break;
                }
                // valid looking env entry? store it
                if (*y == '=') {
                    putenv(x);
                    break;
                }
            }
        }

        x = next;
    }
}

int main(int argc, char** argv) {
    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

    devmgr_io_init();

    root_resource_handle = zx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = zx_job_default();

    printf("devmgr: main()\n");

    devfs_init(root_job_handle);

    zx_object_set_property(root_job_handle, ZX_PROP_NAME, "root", 4);

    zx_status_t status = zx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    zx_object_set_property(svcs_job_handle, ZX_PROP_NAME, "zircon-services", 16);

    status = zx_job_create(root_job_handle, 0u, &fuchsia_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    zx_object_set_property(fuchsia_job_handle, ZX_PROP_NAME, "fuchsia", 7);
    zx_channel_create(0, &svc_root_handle, &svc_request_handle);
    zx_event_create(0, &fshost_event);

    devmgr_vfs_init();

    load_cmdline_from_bootfs();

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    require_system = getenv_bool("devmgr.require-system", false);

    // if we're not a full fuchsia build, no point to set up /svc
    // which will just cause things attempting to access it to block
    // until we give up on the appmgr 10s later
    if (!require_system) {
        devmgr_disable_svc();
    }

    start_console_shell();

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }

    coordinator();
    printf("devmgr: coordinator exited?!\n");
    return 0;
}

static void devmgr_import_bootdata(zx_handle_t vmo) {
    bootdata_t bootdata;
    size_t actual;
    zx_status_t status = zx_vmo_read_old(vmo, &bootdata, 0, sizeof(bootdata), &actual);
    if ((status < 0) || (actual != sizeof(bootdata))) {
        return;
    }
    if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
        printf("devmgr: bootdata item does not contain bootdata\n");
        return;
    }
    if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
        printf("devmgr: bootdata v1 not supported\n");
    }
    size_t len = bootdata.length;
    size_t off = sizeof(bootdata);

    while (len > sizeof(bootdata)) {
        zx_status_t status = zx_vmo_read_old(vmo, &bootdata, off, sizeof(bootdata), &actual);
        if ((status < 0) || (actual != sizeof(bootdata))) {
            break;
        }
        size_t itemlen = BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata.length);
        if (itemlen > len) {
            printf("devmgr: bootdata item too large (%zd > %zd)\n", itemlen, len);
            break;
        }
        switch (bootdata.type) {
        case BOOTDATA_CONTAINER:
            printf("devmgr: unexpected bootdata container header\n");
            return;
        case BOOTDATA_PLATFORM_ID:
            devmgr_set_platform_id(vmo, off + sizeof(bootdata_t), itemlen);
            break;
        default:
            break;
        }
        off += itemlen;
        len -= itemlen;
    }
}

static zx_handle_t fs_root;

static bootfs_t bootfs;

static zx_status_t load_object(void* ctx, const char* name, zx_handle_t* vmo) {
    char tmp[256];
    if (snprintf(tmp, sizeof(tmp), "lib/%s", name) >= (int)sizeof(tmp)) {
        return ZX_ERR_BAD_PATH;
    }
    bootfs_t* bootfs = ctx;
    return bootfs_open(bootfs, tmp, vmo);
}

static zx_status_t load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

static loader_service_ops_t loader_ops = {
    .load_object = load_object,
    .load_abspath = load_abspath,
    .publish_data_sink = publish_data_sink,
};

static loader_service_t* loader_service;

#define MAXHND ZX_CHANNEL_MAX_MSG_HANDLES

void fshost_start(void) {
    zx_handle_t vmo = zx_get_startup_handle(PA_HND(PA_VMO_BOOTFS, 0));
    if ((vmo == ZX_HANDLE_INVALID) ||
        (bootfs_create(&bootfs, vmo) != ZX_OK)) {
        printf("devmgr: cannot find and open bootfs\n");
        exit(1);
    }

    // create a local loader service backed directly by the primary bootfs
    // to allow us to load the fshost (since we don't have filesystems before
    // the fshost starts up).
    zx_handle_t svc;
    if ((loader_service_create(NULL, &loader_ops, &bootfs, &loader_service) != ZX_OK) ||
        (loader_service_connect(loader_service, &svc) != ZX_OK)) {
        printf("devmgr: cannot create loader service\n");
        exit(1);
    }

    // set the bootfs-loader as the default loader service for now
    zx_handle_close(dl_set_loader_service(svc));

    // assemble handles to pass down to fshost
    zx_handle_t handles[MAXHND];
    uint32_t types[MAXHND];
    size_t n = 0;

    // pass /, /dev, and /svc handles to fsboot
    if (zx_channel_create(0, &fs_root, &handles[0]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 0);
    }
    if ((handles[n] = devfs_root_clone()) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 1);
    }
    if ((handles[n] = fs_clone("svc")) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 2);
    }
    if (zx_channel_create(0, &svc, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 3);
    } else {
        svc = ZX_HANDLE_INVALID;
    }

    // pass primary bootfs to fshost
    handles[n] = vmo;
    types[n++] = PA_HND(PA_VMO_BOOTFS, 0);

    // pass fuchsia start event to fshost
    if (zx_handle_duplicate(fshost_event, ZX_RIGHT_SAME_RIGHTS, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER1, 0);
    }

    // pass bootdata VMOs to fshost
    for (size_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_BOOTDATA, m);
        if ((handles[n] = zx_get_startup_handle(type)) != ZX_HANDLE_INVALID) {
            devmgr_import_bootdata(handles[n]);
            types[n++] = type;
        } else {
            break;
        }
    }

    // pass VDSO VMOS to fsboot
    vmo = ZX_HANDLE_INVALID;
    for (size_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_VDSO, m);
        if ((handles[n] = zx_get_startup_handle(type)) != ZX_HANDLE_INVALID) {
            if (m == 0) {
                zx_handle_duplicate(handles[n], ZX_RIGHT_SAME_RIGHTS, &vmo);
            }
            types[n++] = type;
        } else {
            break;
        }
    }

    // pass KERNEL FILE VMOS to fsboot
    for (size_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_KERNEL_FILE, m);
        if ((handles[n] = zx_get_startup_handle(type)) != ZX_HANDLE_INVALID) {
            types[n++] = type;
        } else {
            break;
        }
    }

    launchpad_set_vdso_vmo(vmo);

    const char* argv[] = { "/boot/bin/fshost", "--netboot" };
    int argc = (getenv_bool("netsvc.netboot", false) ||
                getenv_bool("zircon.system.disable-automount", false)) ? 2 : 1;

    // Pass zircon.system.* options to the fshost as environment variables
    const char* envp[16];
    unsigned envc = 0;
    char** e = environ;
    while (*e && (envc < countof(envp))) {
        if (!strncmp(*e, "zircon.system", strlen("zircon.system"))) {
            envp[envc++] = *e;
        }
        e++;
    }
    envp[envc] = NULL;

    devmgr_launch(svcs_job_handle, "fshost", argc, argv,
                  envp, -1, handles, types, n, NULL, 0);

    // switch to system loader service provided by fshost
    zx_handle_close(dl_set_loader_service(svc));
}

zx_handle_t devmgr_load_file(const char* path) {
    if (strncmp(path, "/boot/", 6)) {
        return ZX_HANDLE_INVALID;
    }
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    bootfs_open(&bootfs, path + 6, &vmo);
    return vmo;
}

void devmgr_vfs_exit(void) {
    zx_status_t status;
    if ((status = zx_object_signal(fshost_event, 0, FSHOST_SIGNAL_EXIT)) != ZX_OK) {
        printf("devmgr: Failed to signal VFS exit\n");
        return;
    } else if ((status = zx_object_wait_one(fshost_event,
                                            FSHOST_SIGNAL_EXIT_DONE,
                                            zx_deadline_after(ZX_SEC(5)), NULL)) != ZX_OK) {
        printf("devmgr: Failed to wait for VFS exit completion\n");
    }
}

zx_handle_t fs_clone(const char* path) {
    if (!strcmp(path, "svc")) {
        return fdio_service_clone(svc_root_handle);
    }
    if (!strcmp(path, "dev")) {
        return devfs_root_clone();
    }
    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }
    if (fdio_open_at(fs_root, path, FS_DIR_FLAGS, h1) != ZX_OK) {
        zx_handle_close(h0);
        return ZX_HANDLE_INVALID;
    }
    return h0;
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    fshost_start();

    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) != ZX_OK) {
        printf("devmgr: cannot create namespace: %d\n", r);
        return;
    }
    if ((r = fdio_ns_bind(ns, "/dev", fs_clone("dev"))) != ZX_OK) {
        printf("devmgr: cannot bind /dev to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/boot", fs_clone("boot"))) != ZX_OK) {
        printf("devmgr: cannot bind /boot to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/system", fs_clone("system"))) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", r);
    }
    if ((r = fdio_ns_install(ns)) != ZX_OK) {
        printf("devmgr: cannot install namespace: %d\n", r);
    }
}

