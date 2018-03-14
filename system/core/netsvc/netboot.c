// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <launchpad/launchpad.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <zircon/boot/bootdata.h>
#include <zircon/boot/netboot.h>
#include <zircon/device/dmctl.h>

static uint32_t last_cookie = 0;
static uint32_t last_cmd = 0;
static uint32_t last_arg = 0;
static uint32_t last_ack_cmd = 0;
static uint32_t last_ack_arg = 0;

#define PAGE_ROUNDUP(x) ((x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define MAX_ADVERTISE_DATA_LEN 256

static bool xfer_active = false;

typedef struct nbfilecontainer {
    nbfile file;
    zx_handle_t data;   // handle to vmo that backs netbootfile.
} nbfilecontainer_t;

static nbfilecontainer_t nbkernel;
static nbfilecontainer_t nbbootdata;
static nbfilecontainer_t nbcmdline;

// Pointer to the currently active transfer.
static nbfile* active;

zx_status_t nbfilecontainer_init(size_t size, nbfilecontainer_t* target) {
    zx_status_t st = ZX_OK;

    assert(target);

    // De-init the container if it's already initialized.
    if (target->file.data) {
        // For now, I can't see a valid reason that a client would send the same
        // filename twice.
        // We'll handle this case gracefully, but we'll print a warning to the
        // console just in case it was a mistake.
        printf("netbootloader: warning, reusing a previously initialized container\n");

        // Unmap the vmo from the address space.
        st = zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)target->file.data, target->file.size);
        if (st != ZX_OK) {
            printf("netbootloader: failed to unmap existing vmo, st = %d\n", st);
            return st;
        }

        zx_handle_close(target->data);

        target->file.offset = 0;
        target->file.size = 0;
        target->file.data = 0;
    }

    st = zx_vmo_create(size, 0, &target->data);
    if (st != ZX_OK) {
        printf("netbootloader: Could not create a netboot vmo of size = %lu "
               "retcode = %d\n", size, st);
        return st;
    }

    uintptr_t buffer;
    st = zx_vmar_map(zx_vmar_root_self(), 0, target->data, 0, size,
                     ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &buffer);
    if (st != ZX_OK) {
        printf("netbootloader: failed to map data vmo for buffer, st = %d\n", st);
        zx_handle_close(target->data);
        return st;
    }

    target->file.offset = 0;
    target->file.size = size;
    target->file.data = (uint8_t*)buffer;

    return ZX_OK;
}

nbfile* netboot_get_buffer(const char* name, size_t size) {
    zx_status_t st = ZX_OK;
    nbfilecontainer_t* result;

    if (!strcmp(name, NB_KERNEL_FILENAME)) {
        result = &nbkernel;
    } else if (!strcmp(name, NB_RAMDISK_FILENAME)) {
        result = &nbbootdata;
    } else if (!strcmp(name, NB_CMDLINE_FILENAME)) {
        result = &nbcmdline;
    } else {
        return NULL;
    }

    st = nbfilecontainer_init(size, result);
    if (st != ZX_OK) {
        printf("netbootloader: failed to initialize file container for "
               "file = '%s', retcode = %d\n", name, st);
        return NULL;
    }

    return &result->file;
}

void netboot_advertise(const char* nodename) {
    // Don't advertise if a transfer is active.
    if (xfer_active) return;

    uint8_t buffer[sizeof(nbmsg) + MAX_ADVERTISE_DATA_LEN];
    nbmsg* msg = (void*)buffer;
    msg->magic = NB_MAGIC;
    msg->cookie = 0;
    msg->cmd = NB_ADVERTISE;
    msg->arg = NB_VERSION_CURRENT;

    snprintf((char*)msg->data, MAX_ADVERTISE_DATA_LEN, "version=%s;nodename=%s",
             BOOTLOADER_VERSION, nodename);
    const size_t data_len = strlen((char*)msg->data) + 1;
    udp6_send(buffer, sizeof(nbmsg) + data_len, &ip6_ll_all_nodes,
              NB_ADVERT_PORT, NB_SERVER_PORT, false);
}

static void nb_open(const char* filename, uint32_t cookie, uint32_t arg,
                    const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = netfile_open(filename, arg, NULL);
    udp6_send(&m, sizeof(m), saddr, sport, dport, false);
}

static void nb_read(uint32_t cookie, uint32_t arg,
                    const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    static netfilemsg m = { .hdr.magic = NB_MAGIC, .hdr.cmd = NB_ACK};
    static size_t msg_size = 0;
    static uint32_t blocknum = (uint32_t) -1;
    if (arg == blocknum) {
        // Request to resend last message, verify that the cookie is unchanged
        if (cookie != m.hdr.cookie) {
            m.hdr.arg = -EIO;
            m.hdr.cookie = cookie;
            msg_size = sizeof(m.hdr);
        }
    } else if (arg == 0 || arg == blocknum + 1) {
        int result = netfile_read(&m.data, sizeof(m.data));
        if (result < 0) {
            m.hdr.arg = result;
            msg_size = sizeof(m.hdr);
        } else {
            // Note that the response does not use actual size as the argument,
            // it matches the *requested* size. Actual size can be determined by
            // the packet size.
            m.hdr.arg = arg;
            msg_size = sizeof(m.hdr) + result;
        }
        m.hdr.cookie = cookie;
        blocknum = arg;
    } else {
        // Ignore bogus read requests -- host will timeout if they're confused
        return;
    }
    udp6_send(&m, msg_size, saddr, sport, dport, false);
}

static void nb_write(const char* data, size_t len, uint32_t cookie, uint32_t arg,
                     const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    static nbmsg m =  {.magic = NB_MAGIC, .cmd = NB_ACK};
    static uint32_t blocknum = (uint32_t) -1;
    if (arg == blocknum) {
        // Request to repeat last write, verify that cookie is unchanged
        if (cookie != m.cookie) {
            m.arg = -EIO;
        }
    } else if (arg == 0 || arg == blocknum + 1) {
        int result = netfile_write(data, len);
        m.arg = result > 0 ? 0 : result;
        blocknum = arg;
    }
    m.cookie = cookie;
    udp6_send(&m, sizeof(m), saddr, sport, dport, false);
}

static void nb_close(uint32_t cookie,
                     const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = netfile_close();
    udp6_send(&m, sizeof(m), saddr, sport, dport, false);
}

static zx_status_t do_dmctl_mexec(void) {
    // append the cmdline to the bootdata
    uint32_t section_length = BOOTDATA_ALIGN(nbcmdline.file.size) + sizeof(bootdata_t);
    uint64_t new_size = nbbootdata.file.size + section_length;
    zx_status_t st = zx_vmo_set_size(nbbootdata.data, new_size);
    if (st != ZX_OK) {
        printf("netbootloader: failed to allocate space to append cmdline to bootdata\n");
        return st;
    }

    bootdata_t new_hdr = {
        .type = BOOTDATA_CMDLINE,
        .length = nbcmdline.file.size,
        .extra = 0,
        .flags = BOOTDATA_FLAG_V2,
        .reserved0 = 0,
        .reserved1 = 0,
        .magic = BOOTITEM_MAGIC,
        .crc32 = BOOTITEM_NO_CRC32,
    };
    bootdata_t* hdr = (bootdata_t*)nbbootdata.file.data;

    size_t actual;
    st = zx_vmo_write_old(nbbootdata.data, &new_hdr, hdr->length + sizeof(bootdata_t),
                      sizeof(bootdata_t), &actual);
    if ((st != ZX_OK) || (actual != sizeof(bootdata_t))) {
        printf("netbootloader: failed to write cmdline header\n");
        return st;
    }
    st = zx_vmo_write_old(nbbootdata.data, nbcmdline.file.data,
                      hdr->length + 2 * sizeof(bootdata_t), nbcmdline.file.size, &actual);
    if ((st != ZX_OK) || (actual != nbcmdline.file.size)) {
        printf("netbootloader: failed to write cmdline\n");
        return st;
    }

    hdr->length += section_length;

    zx_handle_t wait_handle;
    st = zx_handle_duplicate(nbkernel.data, ZX_RIGHT_SAME_RIGHTS, &wait_handle);
    if (st != ZX_OK) {
        return st;
    }

    int fd = open("/dev/misc/dmctl", O_WRONLY);
    if (fd < 0) {
        return fd;
    }
    dmctl_mexec_args_t args = {
        .kernel = nbkernel.data,
        .bootdata = nbbootdata.data,
    };
    int r = ioctl_dmctl_mexec(fd, &args);
    close(fd);
    if (r < 0) {
        return r;
    }

    r = zx_object_wait_one(wait_handle, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);
    zx_handle_close(wait_handle);
    if (r != ZX_OK) {
        return r;
    }
    // if we get here, mexec failed
    return ZX_ERR_INTERNAL;
}

static void bootloader_recv(void* data, size_t len,
                            const ip6_addr_t* daddr, uint16_t dport,
                            const ip6_addr_t* saddr, uint16_t sport) {
    nbmsg* msg = data;
    nbmsg ack;

    bool do_transmit = true;
    bool do_boot = false;

    if (dport != NB_SERVER_PORT)
        return;

    if (len < sizeof(nbmsg))
        return;
    len -= sizeof(nbmsg);

    if ((last_cookie == msg->cookie) &&
        (last_cmd == msg->cmd) && (last_arg == msg->arg)) {
        // host must have missed the ack. resend
        ack.magic = NB_MAGIC;
        ack.cookie = last_cookie;
        ack.cmd = last_ack_cmd;
        ack.arg = last_ack_arg;
        goto transmit;
    }

    ack.cmd = NB_ACK;
    ack.arg = 0;

    switch (msg->cmd) {
    case NB_COMMAND:
        if (len == 0)
            return;
        msg->data[len - 1] = 0;
        break;
    case NB_SEND_FILE:
        xfer_active = true;
        if (len == 0)
            return;
        msg->data[len - 1] = 0;
        for (size_t i = 0; i < (len - 1); i++) {
            if ((msg->data[i] < ' ') || (msg->data[i] > 127)) {
                msg->data[i] = '.';
            }
        }
        active = netboot_get_buffer((const char*)msg->data, msg->arg);
        if (active) {
            active->offset = 0;
            ack.arg = msg->arg;
            size_t prefix_len = strlen(NB_FILENAME_PREFIX);
            const char* filename;
            if (!strncmp((char*)msg->data, NB_FILENAME_PREFIX, prefix_len)) {
                filename = &((const char*)msg->data)[prefix_len];
            } else {
                filename = (const char*)msg->data;
            }
            printf("netboot: Receive File '%s'...\n", filename);
        } else {
            printf("netboot: Rejected File '%s'...\n", (char*) msg->data);
            ack.cmd = NB_ERROR_BAD_FILE;
        }
        break;

    case NB_DATA:
    case NB_LAST_DATA:
        xfer_active = true;
        if (active == 0) {
            printf("netboot: > received chunk before NB_FILE\n");
            return;
        }
        if (msg->arg != active->offset) {
            // printf("netboot: < received chunk at offset %d but current offset is %zu\n", msg->arg, active->offset);
            ack.arg = active->offset;
            ack.cmd = NB_ACK;
        } else if ((active->offset + len) > active->size) {
            ack.cmd = NB_ERROR_TOO_LARGE;
            ack.arg = msg->arg;
        } else {
            memcpy(active->data + active->offset, msg->data, len);
            active->offset += len;
            ack.cmd = msg->cmd == NB_LAST_DATA ? NB_FILE_RECEIVED : NB_ACK;
            if (msg->cmd != NB_LAST_DATA) {
                do_transmit = false;
            } else {
                xfer_active = false;
            }
        }
        break;
    case NB_BOOT:
        do_boot = true;
        // Wait for the paver to complete
        while (atomic_load(&paving_in_progress)) {
            thrd_yield();
        }
        printf("netboot: Boot Kernel...\n");
        break;
    default:
        // We don't have a handler for this command, let netsvc handle it.
        do_transmit = false;
    }

    last_cookie = msg->cookie;
    last_cmd = msg->cmd;
    last_arg = msg->arg;
    last_ack_cmd = ack.cmd;
    last_ack_arg = ack.arg;

    ack.cookie = msg->cookie;
    ack.magic = NB_MAGIC;
transmit:
    if (do_transmit) {
        udp6_send(&ack, sizeof(ack), saddr, sport, NB_SERVER_PORT, false);
    }

    if (do_boot) {
        if (do_dmctl_mexec() != ZX_OK) {
            // TODO: This will return before the system actually mexecs.
            // We can't pass an event to wait on here because fdio
            // has a limit of 3 handles, and we're already using
            // all 3 to pass boot parameters.
            printf("netboot: Boot failed\n");
        }
    }
}

void netboot_recv(void *data, size_t len, bool is_mcast,
                  const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport) {
    nbmsg* msg = data;
    // Not enough bytes to be a message
    if ((len < sizeof(*msg)) ||
        (msg->magic != NB_MAGIC)) {
        return;
    }
    len -= sizeof(*msg);

    if (len && msg->cmd != NB_DATA && msg->cmd != NB_LAST_DATA) {
        msg->data[len - 1] = '\0';
    }

    switch (msg->cmd) {
    case NB_QUERY:
        if (strcmp((char*)msg->data, "*") &&
            strcmp((char*)msg->data, nodename)) {
            break;
        }
        size_t dlen = strlen(nodename) + 1;
        char buf[1024 + sizeof(nbmsg)];
        if ((dlen + sizeof(nbmsg)) > sizeof(buf)) {
            return;
        }
        msg->cmd = NB_ACK;
        memcpy(buf, msg, sizeof(nbmsg));
        memcpy(buf + sizeof(nbmsg), nodename, dlen);
        udp6_send(buf, sizeof(nbmsg) + dlen, saddr, sport, dport, false);
        break;
    case NB_SHELL_CMD:
        if (!is_mcast) {
            netboot_run_cmd((char*) msg->data);
            return;
        }
        break;
    case NB_OPEN:
        nb_open((char*)msg->data, msg->cookie, msg->arg, saddr, sport, dport);
        break;
    case NB_READ:
        nb_read(msg->cookie, msg->arg, saddr, sport, dport);
        break;
    case NB_WRITE:
        len--; // NB NUL-terminator is not part of the data
        nb_write((char*)msg->data, len, msg->cookie, msg->arg, saddr, sport, dport);
        break;
    case NB_CLOSE:
        nb_close(msg->cookie, saddr, sport, dport);
        break;
    default:
        // If the bootloader is enabled, then let it have a crack at the
        // incoming packets as well.
        if (netbootloader) {
            bootloader_recv(data, len + sizeof(nbmsg), daddr, dport, saddr, sport);
        }
    }
}
