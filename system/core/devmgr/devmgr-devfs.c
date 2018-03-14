// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zircon/device/vfs.h>

#include <fdio/io.fidl2.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct dc_watcher watcher_t;
typedef struct dc_iostate iostate_t;

struct dc_watcher {
    watcher_t* next;
    devnode_t* devnode;
    uint32_t mask;
    zx_handle_t handle;
};

struct dc_devnode {
    const char* name;
    uint64_t ino;

    // NULL if we are a pure directory node,
    // otherwise the device we are referencing
    device_t* device;

    watcher_t* watchers;

    // entry in our parent devnode's children list
    list_node_t node;

    // list of our child devnodes
    list_node_t children;

    // list of attached iostates
    list_node_t iostate;

    // used to assign unique small device numbers
    // for class device links
    uint32_t seqcount;
};

struct dc_iostate {
    port_handler_t ph;

    // entry in our devnode's iostate list
    list_node_t node;

    // pointer to our devnode, NULL if it has been removed
    devnode_t* devnode;

    uint64_t readdir_ino;
};

extern port_t dc_port;

static uint64_t next_ino = 2;

static devnode_t root_devnode = {
    .name = "",
    .ino = 1,
    .children = LIST_INITIAL_VALUE(root_devnode.children),
    .iostate = LIST_INITIAL_VALUE(root_devnode.iostate),
};

static devnode_t* class_devnode;

static zx_status_t dc_rio_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt);
static devnode_t* devfs_mkdir(devnode_t* parent, const char* name);

#define PNMAX 16
static const char* proto_name(uint32_t id, char buf[PNMAX]) {
    switch (id) {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) case val: return name;
#include <ddk/protodefs.h>
    default:
        snprintf(buf, PNMAX, "proto-%08x", id);
        return buf;
    }
}

typedef struct {
    const char* name;
    devnode_t* devnode;
    uint32_t id;
    uint32_t flags;
} pinfo_t;

static pinfo_t proto_info[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) { name, NULL, val, flags },
#include <ddk/protodefs.h>
    { NULL, NULL, 0, 0 },
};

static devnode_t* proto_dir(uint32_t id) {
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (info->id == id) {
            return info->devnode;
        }
    }
    return NULL;
}

static void prepopulate_protocol_dirs(void) {
    class_devnode = devfs_mkdir(&root_devnode, "class");
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (!(info->flags & PF_NOPUB)) {
            info->devnode = devfs_mkdir(class_devnode, info->name);
        }
    }
}

void describe_error(zx_handle_t h, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = ZXRIO_ON_OPEN;
    msg.status = status;
    zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), NULL, 0);
    zx_handle_close(h);
}

static zx_status_t iostate_create(devnode_t* dn, zx_handle_t h) {
    iostate_t* ios = calloc(1, sizeof(iostate_t));
    if (ios == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    ios->ph.handle = h;
    ios->ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dc_rio_handler;
    ios->devnode = dn;
    list_add_tail(&dn->iostate, &ios->node);

    zx_status_t r;
    if ((r = port_wait(&dc_port, &ios->ph)) < 0) {
        list_delete(&ios->node);
        free(ios);
    }
    return r;
}

static void iostate_destroy(iostate_t* ios) {
    if (ios->devnode) {
        list_delete(&ios->node);
        ios->devnode = NULL;
    }
    zx_handle_close(ios->ph.handle);
    ios->ph.handle = ZX_HANDLE_INVALID;
    free(ios);
}

// A devnode is a directory (from stat's perspective) if
// it has children, or if it doesn't have a device, or if
// its device has no rpc handle
static bool devnode_is_dir(devnode_t* dn) {
    if (list_is_empty(&dn->children)) {
        return (dn->device == NULL) || (dn->device->hrpc == ZX_HANDLE_INVALID);
    }
    return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying devhost
static bool devnode_is_local(devnode_t* dn) {
    if (dn->device == NULL) {
        return true;
    }
    if (dn->device->hrpc == ZX_HANDLE_INVALID) {
        return true;
    }
    if (dn->device->flags & DEV_CTX_MUST_ISOLATE) {
        return true;
    }
    return false;
}

static void devfs_notify(devnode_t* dn, const char* name, unsigned op) {
    watcher_t* w = dn->watchers;
    if (w == NULL) {
        return;
    }

    size_t len = strlen(name);
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    uint8_t msg[VFS_WATCH_NAME_MAX + 2];
    msg[0] = op;
    msg[1] = len;
    memcpy(msg + 2, name, len);

    // convert to mask
    op = (1u << op);

    watcher_t** wp;
    watcher_t* next;
    for (wp = &dn->watchers; w != NULL; w = next) {
        next = w->next;
        if (!(w->mask & op)) {
            continue;
        }
        if (zx_channel_write(w->handle, 0, msg, len + 2, NULL, 0) < 0) {
            *wp = next;
            zx_handle_close(w->handle);
            free(w);
        } else {
            wp = &w->next;
        }
    }
}

static zx_status_t devfs_watch(devnode_t* dn, zx_handle_t h, uint32_t mask) {
    watcher_t* watcher = calloc(1, sizeof(watcher_t));
    if (watcher == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    watcher->devnode = dn;
    watcher->next = dn->watchers;
    watcher->handle = h;
    watcher->mask = mask;
    dn->watchers = watcher;

    if (mask & VFS_WATCH_MASK_EXISTING) {
        devnode_t* child;
        list_for_every_entry(&dn->children, child, devnode_t, node) {
            if (child->device && (child->device->flags & DEV_CTX_INVISIBLE)) {
                continue;
            }
            //TODO: send multiple per write
            devfs_notify(dn, child->name, VFS_WATCH_EVT_EXISTING);
        }
        devfs_notify(dn, "", VFS_WATCH_EVT_IDLE);

    }

    // Don't send EXISTING or IDLE events from now on...
    watcher->mask &= ~(VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE);

    return ZX_OK;
}

// If namelen is nonzero, it is the null-terminator-inclusive length
// of name, which should be copied into the devnode.  Otherwise name
// is guaranteed to exist for the lifetime of the devnode.
static devnode_t* devfs_mknode(device_t* dev, const char* name, size_t namelen) {
    devnode_t* dn = calloc(1, sizeof(devnode_t) + namelen);
    if (dn == NULL) {
        return NULL;
    }
    if (namelen > 0) {
        char* p = (char*) (dn + 1);
        memcpy(p, name, namelen);
        dn->name = p;
    } else {
        dn->name = name;
    }
    dn->ino = next_ino++;
    dn->device = dev;
    list_initialize(&dn->children);
    list_initialize(&dn->iostate);
    return dn;
}

static devnode_t* devfs_mkdir(devnode_t* parent, const char* name) {
    devnode_t* dn = devfs_mknode(NULL, name, 0);
    if (dn == NULL) {
        return NULL;
    }
    list_add_tail(&parent->children, &dn->node);
    return dn;
}

static devnode_t* devfs_lookup(devnode_t* parent, const char* name) {
    devnode_t* child;
    list_for_every_entry(&parent->children, child, devnode_t, node) {
        if (!strcmp(name, child->name)) {
            return child;
        }
    }
    return NULL;
}

void devfs_advertise(device_t* dev) {
    if (dev->link) {
        devnode_t* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, VFS_WATCH_EVT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, VFS_WATCH_EVT_ADDED);
    }
}

// TODO: generate a MODIFIED event rather than back to back REMOVED and ADDED
void devfs_advertise_modified(device_t* dev) {
    if (dev->link) {
        devnode_t* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, VFS_WATCH_EVT_REMOVED);
        devfs_notify(dir, dev->link->name, VFS_WATCH_EVT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, VFS_WATCH_EVT_REMOVED);
        devfs_notify(dev->parent->self, dev->self->name, VFS_WATCH_EVT_ADDED);
    }
}

zx_status_t devfs_publish(device_t* parent, device_t* dev) {
    if ((parent->self == NULL) || (dev->self != NULL) || (dev->link != NULL)) {
        return ZX_ERR_INTERNAL;
    }

    devnode_t* dnself = devfs_mknode(dev, dev->name, 0);
    if (dnself == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_MISC)) {
        // misc devices are singletons, not a class
        // in the sense of other device classes.
        // They do not get aliases in /dev/class/misc/...
        // instead they exist only under their parent
        // device.
        goto done;
    }

    // Create link in /dev/class/... if this id has a published class
    devnode_t* dir = proto_dir(dev->protocol_id);
    if (dir != NULL) {
        char tmp[32];
        const char* name = dev->name;
        size_t namelen = 0;

        if (dev->protocol_id != ZX_PROTOCOL_CONSOLE) {

            for (unsigned n = 0; n < 1000; n++) {
                snprintf(tmp, sizeof(tmp), "%03u", (dir->seqcount++) % 1000);
                if (devfs_lookup(dir, tmp) == NULL) {
                    name = tmp;
                    namelen = 4;
                    goto got_name;
                }
            }
            free(dnself);
            return ZX_ERR_ALREADY_EXISTS;
got_name:
            ;
        }

        devnode_t* dnlink = devfs_mknode(dev, name, namelen);
        if (dnlink == NULL) {
            free(dnself);
            return ZX_ERR_NO_MEMORY;
        }

        // add link node to class directory
        list_add_tail(&dir->children, &dnlink->node);
        dev->link = dnlink;
    }

done:
    // add self node to parent directory
    list_add_tail(&parent->self->children, &dnself->node);
    dev->self = dnself;

    if (!(dev->flags & DEV_CTX_INVISIBLE)) {
        devfs_advertise(dev);
    }
    return ZX_OK;
}

static void _devfs_remove(devnode_t* dn) {
    if (list_in_list(&dn->node)) {
        list_delete(&dn->node);
    }

    // detach all connected iostates
    iostate_t* ios;
    list_for_every_entry(&dn->iostate, ios, iostate_t, node) {
        ios->devnode = NULL;
        zx_handle_close(ios->ph.handle);
        ios->ph.handle = ZX_HANDLE_INVALID;
    }

    // notify own file watcher
    if ((dn->device == NULL) ||
        !(dn->device->flags & DEV_CTX_INVISIBLE)) {
        devfs_notify(dn, "", VFS_WATCH_EVT_DELETED);
    }

    // disconnect from device and notify parent/link directory watchers
    if (dn->device != NULL) {
        if (dn->device->self == dn) {
            dn->device->self = NULL;

            if ((dn->device->parent != NULL) &&
                (dn->device->parent->self != NULL) &&
                !(dn->device->flags & DEV_CTX_INVISIBLE)) {
                devfs_notify(dn->device->parent->self, dn->name, VFS_WATCH_EVT_REMOVED);
            }
        }
        if (dn->device->link == dn) {
            dn->device->link = NULL;

            if (!(dn->device->flags & DEV_CTX_INVISIBLE)) {
                devnode_t* dir = proto_dir(dn->device->protocol_id);
                devfs_notify(dir, dn->name, VFS_WATCH_EVT_REMOVED);
            }
        }
        dn->device = NULL;
    }

    // destroy all watchers
    watcher_t* watcher;
    watcher_t* next;
    for (watcher = dn->watchers; watcher != NULL; watcher = next) {
        next = watcher->next;
        zx_handle_close(watcher->handle);
        free(watcher);
    }
    dn->watchers = NULL;

    // detach children
    while (list_remove_head(&dn->children) != NULL) {
        // they will be unpublished when the devices they're
        // associated with are eventually destroyed
    }
}

void devfs_unpublish(device_t* dev) {
    if (dev->self != NULL) {
        _devfs_remove(dev->self);
        dev->self = NULL;
    }
    if (dev->link != NULL) {
        _devfs_remove(dev->link);
        dev->link = NULL;
    }
}

static zx_status_t devfs_walk(devnode_t** _dn, char* path, char** pathout) {
    devnode_t* dn = *_dn;

again:
    if ((path == NULL) || (path[0] == 0)) {
        *_dn = dn;
        return ZX_OK;
    }
    char* name = path;
    char* undo = NULL;
    if ((path = strchr(path, '/')) != NULL) {
        undo = path;
        *path++ = 0;
    }
    if (name[0] == 0) {
        return ZX_ERR_BAD_PATH;
    }
    devnode_t* child;
    list_for_every_entry(&dn->children, child, devnode_t, node) {
        if (!strcmp(child->name, name)) {
            if(child->device && (child->device->flags & DEV_CTX_INVISIBLE)) {
                continue;
            }
            dn = child;
            goto again;
        }
    }
    if (dn == *_dn) {
        return ZX_ERR_NOT_FOUND;
    }
    if (undo) {
        *undo = '/';
    }
    *_dn = dn;
    *pathout = name;
    return ZX_ERR_NEXT;
}

static void devfs_open(devnode_t* dirdn, zx_handle_t h, char* path, uint32_t flags) {
    if (!strcmp(path, ".")) {
        path = NULL;
    }

    devnode_t* dn = dirdn;
    zx_status_t r = devfs_walk(&dn, path, &path);

    bool describe = flags & ZX_FS_FLAG_DESCRIBE;

    if (r == ZX_ERR_NEXT) {
        // we only partially matched -- there's more path to walk
        if ((dn->device == NULL) || (dn->device->hrpc == ZX_HANDLE_INVALID)) {
            // no remote to pass this on to
            r = ZX_ERR_NOT_FOUND;
        } else if (flags & (ZX_FS_FLAG_NOREMOTE | ZX_FS_FLAG_DIRECTORY)) {
            // local requested, but this is remote only
            r = ZX_ERR_NOT_SUPPORTED;
        } else {
            r = ZX_OK;
        }
    } else {
        path = (char*) ".";
    }

    if (r < 0) {
fail:
        if (describe) {
            describe_error(h, r);
        } else {
            zx_handle_close(h);
        }
        return;
    }

    // If we are a local-only node, or we are asked to not go remote,
    // or we are asked to open-as-a-directory, open locally:
    if ((flags & (ZX_FS_FLAG_NOREMOTE | ZX_FS_FLAG_DIRECTORY)) || devnode_is_local(dn)) {
        if ((r = iostate_create(dn, h)) < 0) {
            goto fail;
        }
        if (describe) {
            zxrio_describe_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.op = ZXRIO_ON_OPEN;
            msg.status = ZX_OK;
            msg.extra.tag = FDIO_PROTOCOL_DIRECTORY;
            zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), NULL, 0);
        }
        return;
    }

    // Otherwise we will pass the request on to the remote
    zxrio_msg_t msg;
    uint32_t msize;
#ifdef ZXRIO_FIDL
    DirectoryOpenMsg* request = (DirectoryOpenMsg*) &msg;
    memset(request, 0, sizeof(DirectoryOpenMsg));
    request->hdr.ordinal = ZXFIDL_OPEN;
    request->path.size = strlen(path);
    request->path.data = (char*) FIDL_ALLOC_PRESENT;
    request->flags = flags;
    request->object = FIDL_HANDLE_PRESENT;
    void* secondary = (void*)((uintptr_t)(request) +
                              FIDL_ALIGN(sizeof(DirectoryOpenMsg)));
    memcpy(secondary, path, request->path.size);
    msize = FIDL_ALIGN(sizeof(DirectoryOpenMsg)) + FIDL_ALIGN(request->path.size);
#else
    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = ZXRIO_OPEN;
    msg.datalen = strlen(path);
    msg.arg = flags;
    msg.hcount = 1;
    msg.handle[0] = h;
    memcpy(msg.data, path, msg.datalen);
    msize = ZXRIO_HDR_SZ + msg.datalen;
#endif
    if ((r = zx_channel_write(dn->device->hrpc, 0, &msg, msize, &h, 1)) < 0) {
        goto fail;
    }
}

// Double-check that OPEN (the only message we forward)
// cannot be mistaken for an internal dev coordinator RPC message
static_assert((ZXRIO_OPEN & DC_OP_ID_BIT) == 0, "");
static_assert((ZXFIDL_OPEN & DC_OP_ID_BIT) == 0, "");

static zx_status_t fill_dirent(vdirent_t* de, size_t delen,
                               const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ZX_ERR_INVALID_ARGS;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static zx_status_t devfs_readdir(devnode_t* dn, uint64_t* _ino, void* data, size_t len) {
    void* ptr = data;
    uint64_t ino = *_ino;

    devnode_t* child;
    list_for_every_entry(&dn->children, child, devnode_t, node) {
        if (child->ino <= ino) {
            continue;
        }
        if (child->device == NULL) {
            // "pure" directories (like /dev/class/$NAME) do not show up
            // if they have no children, to avoid clutter and confusion.
            // They remain openable, so they can be watched.
            if (list_is_empty(&child->children)) {
                continue;
            }
        } else {
            // invisible devices also do not show up
            if (child->device->flags & DEV_CTX_INVISIBLE) {
                continue;
            }
        }
        ino = child->ino;
        zx_status_t r = fill_dirent(ptr, len, child->name, strlen(child->name),
                                    VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    *_ino = ino;
    return ptr - data;
}

static zx_status_t devfs_rio_handler(zxrio_msg_t* msg, void* cookie) {
    iostate_t* ios = cookie;
    devnode_t* dn = ios->devnode;
    if (dn == NULL) {
        return ZX_ERR_PEER_CLOSED;
    }

    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;

    if (!ZXRIO_FIDL_MSG(msg->op)) {
        // ensure handle count specified by opcode matches reality
        if (msg->hcount != ZXRIO_HC(msg->op)) {
            return ZX_ERR_IO;
        }
        msg->hcount = 0;
        msg->datalen = 0;
    }

    zx_status_t r;
    switch (ZXRIO_OP(msg->op)) {
    case ZXFIDL_CLONE:
    case ZXRIO_CLONE: {
        ObjectCloneMsg* request = (ObjectCloneMsg*) msg;
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        zx_handle_t h;
        uint32_t flags;
        if (fidl) {
            h = request->object;
            flags = request->flags;
        } else {
            h = msg->handle[0];
            flags = arg;
        }
        char path[PATH_MAX];
        path[0] = '\0';
        devfs_open(dn, h, path, flags | ZX_FS_FLAG_NOREMOTE);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_OPEN:
    case ZXRIO_OPEN: {
        DirectoryOpenMsg* request = (DirectoryOpenMsg*) msg;
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        zx_handle_t h;
        char* path;
        uint32_t flags;
        if (fidl) {
            len = request->path.size;
            path = request->path.data;
            h = request->object;
            flags = request->flags;
        } else {
            path = (char*) msg->data;
            h = msg->handle[0];
            flags = arg;
        }
        if ((len < 1) || (len > 1024)) {
            zx_handle_close(h);
        } else {
            path[len] = '\0';
            devfs_open(dn, h, path, flags);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_STAT:
    case ZXRIO_STAT: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        NodeGetAttrRsp* response = (NodeGetAttrRsp*) msg;

        uint32_t mode;
        if (devnode_is_dir(dn)) {
            mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
        } else {
            mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        }

        if (fidl) {
            memset(&response->attributes, 0, sizeof(response->attributes));
            response->attributes.mode = mode;
            response->attributes.content_size = 0;
            response->attributes.link_count = 1;
            response->attributes.id = dn->ino;
            return ZX_OK;
        }

        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*)msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        attr->mode = mode;
        attr->size = 0;
        attr->nlink = 1;
        attr->inode = dn->ino;
        return msg->datalen;
    }
    case ZXFIDL_REWIND: {
        ios->readdir_ino = 0;
        return ZX_OK;
    }
    case ZXFIDL_READDIR:
    case ZXRIO_READDIR: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        DirectoryReadDirentsMsg* request = (DirectoryReadDirentsMsg*) msg;
        DirectoryReadDirentsRsp* response = (DirectoryReadDirentsRsp*) msg;
        uint32_t max_out;
        void* data;

        if (fidl) {
            data = (void*)((uintptr_t)(response) + FIDL_ALIGN(sizeof(DirectoryReadDirentsRsp)));
            max_out = request->max_out;
        } else {
            max_out = arg;
            if (msg->arg2.off == READDIR_CMD_RESET) {
                ios->readdir_ino = 0;
            }
            data = msg->data;
        }

        if (max_out > FDIO_CHUNK_SIZE) {
            return ZX_ERR_INVALID_ARGS;
        }
        r = devfs_readdir(dn, &ios->readdir_ino, data, max_out);
        if (r >= 0) {
            if (fidl) {
                response->dirents.count = r;
                r = ZX_OK;
            } else {
                msg->datalen = r;
            }
        }
        return r;
    }
    case ZXFIDL_IOCTL:
    case ZXRIO_IOCTL:
    case ZXRIO_IOCTL_1H: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        NodeIoctlMsg* request = (NodeIoctlMsg*) msg;
        NodeIoctlRsp* response = (NodeIoctlRsp*) msg;
        void* secondary = (void*)((uintptr_t)(msg) + FIDL_ALIGN(sizeof(NodeIoctlRsp)));

        uint32_t op;
        void* in_data;
        uint32_t inlen;
        void* out_data;
        uint32_t outmax;
        zx_handle_t* handles;
        if (fidl) {
            op = request->opcode;
            in_data = request->in.data;
            inlen = request->in.count;
            out_data = secondary;
            outmax = request->max_out;
            handles = (zx_handle_t*) request->handles.data;
        } else {
            op = msg->arg2.op;
            in_data = msg->data;
            inlen = len;
            out_data = msg->data;
            outmax = arg;
            handles = msg->handle;
        }

        switch (op) {
        case IOCTL_VFS_WATCH_DIR: {
            vfs_watch_dir_t* wd = (vfs_watch_dir_t*) in_data;
            if ((inlen != sizeof(vfs_watch_dir_t)) ||
                (wd->options != 0) ||
                (wd->mask & (~VFS_WATCH_MASK_ALL))) {
                r = ZX_ERR_INVALID_ARGS;
            } else {
                r = devfs_watch(dn, handles[0], wd->mask);
            }
            if (r != ZX_OK) {
                zx_handle_close(handles[0]);
            }
            if (fidl) {
                r = r > 0 ? ZX_OK : r;
                response->handles.count = 0;
                response->out.count = 0;
            }
            return r;
        }
        case IOCTL_VFS_QUERY_FS: {
            const char* devfs_name = "devfs";
            if (outmax < sizeof(vfs_query_info_t) + strlen(devfs_name)) {
                return ZX_ERR_INVALID_ARGS;
            }
            vfs_query_info_t* info = (vfs_query_info_t*) out_data;
            memset(info, 0, sizeof(*info));
            memcpy(info->name, devfs_name, strlen(devfs_name));
            size_t outlen = sizeof(vfs_query_info_t) + strlen(devfs_name);
            if (fidl) {
                response->handles.count = 0;
                response->out.count = outlen;
                response->out.data = secondary;
                r = ZX_OK;
            } else {
                msg->datalen = outlen;
                r = outlen;
            }
            return r;
        }
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    }

    // close inbound handles so they do not leak
    for (unsigned i = 0; i < ZXRIO_HC(msg->op); i++) {
        zx_handle_close(msg->handle[i]);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dc_rio_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    iostate_t* ios = containerof(ph, iostate_t, ph);

    zx_status_t r;
    zxrio_msg_t msg;
    if (signals & ZX_CHANNEL_READABLE) {
        if ((r = zxrio_handle_rpc(ph->handle, &msg, devfs_rio_handler, ios)) == ZX_OK) {
            return ZX_OK;
        }
    } else if (signals & ZX_CHANNEL_PEER_CLOSED) {
        zxrio_handle_close(devfs_rio_handler, ios);
        r = ZX_ERR_STOP;
    } else {
        printf("dc_rio_handler: invalid signals %x\n", signals);
        exit(0);
    }

    iostate_destroy(ios);
    return r;
}

static zx_handle_t devfs_root;

zx_handle_t devfs_root_clone(void) {
    return fdio_service_clone(devfs_root);
}

void devfs_init(zx_handle_t root_job) {
    printf("devmgr: init\n");

    prepopulate_protocol_dirs();

    root_devnode.device = coordinator_init(root_job);
    root_devnode.device->self = &root_devnode;

    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) != ZX_OK) {
        return;
    } else if (iostate_create(&root_devnode, h0) != ZX_OK) {
        zx_handle_close(h0);
        zx_handle_close(h1);
        return;
    }

    devfs_root = h1;
}

