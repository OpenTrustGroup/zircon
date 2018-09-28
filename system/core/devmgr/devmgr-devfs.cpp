// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/device/vfs.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fidl/coding.h>

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

    // nullptr if we are a pure directory node,
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

    // pointer to our devnode, nullptr if it has been removed
    devnode_t* devnode;

    uint64_t readdir_ino;
};

extern port_t dc_port;

static uint64_t next_ino = 2;

static devnode_t root_devnode = []() {
    devnode_t node = {};
    node.name = "";
    node.ino = 1;
    node.children = LIST_INITIAL_VALUE(root_devnode.children);
    node.iostate = LIST_INITIAL_VALUE(root_devnode.iostate);
    return node;
}();

static devnode_t* class_devnode;

static zx_status_t dc_fidl_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt);
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
#define DDK_PROTOCOL_DEF(tag, val, name, flags) { name, nullptr, val, flags },
#include <ddk/protodefs.h>
    { nullptr, nullptr, 0, 0 },
};

static devnode_t* proto_dir(uint32_t id) {
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (info->id == id) {
            return info->devnode;
        }
    }
    return nullptr;
}

static void prepopulate_protocol_dirs() {
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
    msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    msg.status = status;
    zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
    zx_handle_close(h);
}

static zx_status_t iostate_create(devnode_t* dn, zx_handle_t h) {
    iostate_t* ios = static_cast<iostate_t*>(calloc(1, sizeof(iostate_t)));
    if (ios == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    ios->ph.handle = h;
    ios->ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dc_fidl_handler;
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
        ios->devnode = nullptr;
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
        return (dn->device == nullptr) || (dn->device->hrpc == ZX_HANDLE_INVALID);
    }
    return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying devhost
static bool devnode_is_local(devnode_t* dn) {
    if (dn->device == nullptr) {
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
    if (w == nullptr) {
        return;
    }

    size_t len = strlen(name);
    if (len > fuchsia_io_MAX_FILENAME) {
        return;
    }

    uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
    msg[0] = static_cast<uint8_t>(op);
    msg[1] = static_cast<uint8_t>(len);
    memcpy(msg + 2, name, len);

    // convert to mask
    op = (1u << op);

    watcher_t** wp;
    watcher_t* next;
    for (wp = &dn->watchers; w != nullptr; w = next) {
        next = w->next;
        if (!(w->mask & op)) {
            continue;
        }
        if (zx_channel_write(w->handle, 0, msg, static_cast<uint32_t>(len + 2), nullptr, 0) < 0) {
            *wp = next;
            zx_handle_close(w->handle);
            free(w);
        } else {
            wp = &w->next;
        }
    }
}

static zx_status_t devfs_watch(devnode_t* dn, zx_handle_t h, uint32_t mask) {
    watcher_t* watcher = static_cast<watcher_t*>(calloc(1, sizeof(watcher_t)));
    if (watcher == nullptr) {
        zx_handle_close(h);
        return ZX_ERR_NO_MEMORY;
    }

    watcher->devnode = dn;
    watcher->next = dn->watchers;
    watcher->handle = h;
    watcher->mask = mask;
    dn->watchers = watcher;

    if (mask & fuchsia_io_WATCH_MASK_EXISTING) {
        devnode_t* child;
        list_for_every_entry(&dn->children, child, devnode_t, node) {
            if (child->device && (child->device->flags & DEV_CTX_INVISIBLE)) {
                continue;
            }
            //TODO: send multiple per write
            devfs_notify(dn, child->name, fuchsia_io_WATCH_EVENT_EXISTING);
        }
        devfs_notify(dn, "", fuchsia_io_WATCH_EVENT_IDLE);

    }

    // Don't send EXISTING or IDLE events from now on...
    watcher->mask &= ~(fuchsia_io_WATCH_MASK_EXISTING | fuchsia_io_WATCH_MASK_IDLE);

    return ZX_OK;
}

// If namelen is nonzero, it is the null-terminator-inclusive length
// of name, which should be copied into the devnode.  Otherwise name
// is guaranteed to exist for the lifetime of the devnode.
static devnode_t* devfs_mknode(device_t* dev, const char* name, size_t namelen) {
    devnode_t* dn = static_cast<devnode_t*>(calloc(1, sizeof(devnode_t) + namelen));
    if (dn == nullptr) {
        return nullptr;
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
    devnode_t* dn = devfs_mknode(nullptr, name, 0);
    if (dn == nullptr) {
        return nullptr;
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
    return nullptr;
}

void devfs_advertise(device_t* dev) {
    if (dev->link) {
        devnode_t* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
}

// TODO: generate a MODIFIED event rather than back to back REMOVED and ADDED
void devfs_advertise_modified(device_t* dev) {
    if (dev->link) {
        devnode_t* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_REMOVED);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_REMOVED);
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
}

zx_status_t devfs_publish(device_t* parent, device_t* dev) {
    if ((parent->self == nullptr) || (dev->self != nullptr) || (dev->link != nullptr)) {
        return ZX_ERR_INTERNAL;
    }

    devnode_t* dnself = devfs_mknode(dev, dev->name, 0);
    if (dnself == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((dev->protocol_id == ZX_PROTOCOL_TEST_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_MISC)) {
        // misc devices are singletons, not a class
        // in the sense of other device classes.
        // They do not get aliases in /dev/class/misc/...
        // instead they exist only under their parent
        // device.
        goto done;
    }

    // Create link in /dev/class/... if this id has a published class
    devnode_t* dir;
    dir = proto_dir(dev->protocol_id);
    if (dir != nullptr) {
        char tmp[32];
        const char* name = dev->name;
        size_t namelen = 0;

        if (dev->protocol_id != ZX_PROTOCOL_CONSOLE) {

            for (unsigned n = 0; n < 1000; n++) {
                snprintf(tmp, sizeof(tmp), "%03u", (dir->seqcount++) % 1000);
                if (devfs_lookup(dir, tmp) == nullptr) {
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
        if (dnlink == nullptr) {
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
        ios->devnode = nullptr;
        zx_handle_close(ios->ph.handle);
        ios->ph.handle = ZX_HANDLE_INVALID;
    }

    // notify own file watcher
    if ((dn->device == nullptr) ||
        !(dn->device->flags & DEV_CTX_INVISIBLE)) {
        devfs_notify(dn, "", fuchsia_io_WATCH_EVENT_DELETED);
    }

    // disconnect from device and notify parent/link directory watchers
    if (dn->device != nullptr) {
        if (dn->device->self == dn) {
            dn->device->self = nullptr;

            if ((dn->device->parent != nullptr) &&
                (dn->device->parent->self != nullptr) &&
                !(dn->device->flags & DEV_CTX_INVISIBLE)) {
                devfs_notify(dn->device->parent->self, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
            }
        }
        if (dn->device->link == dn) {
            dn->device->link = nullptr;

            if (!(dn->device->flags & DEV_CTX_INVISIBLE)) {
                devnode_t* dir = proto_dir(dn->device->protocol_id);
                devfs_notify(dir, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
            }
        }
        dn->device = nullptr;
    }

    // destroy all watchers
    watcher_t* watcher;
    watcher_t* next;
    for (watcher = dn->watchers; watcher != nullptr; watcher = next) {
        next = watcher->next;
        zx_handle_close(watcher->handle);
        free(watcher);
    }
    dn->watchers = nullptr;

    // detach children
    while (list_remove_head(&dn->children) != nullptr) {
        // they will be unpublished when the devices they're
        // associated with are eventually destroyed
    }
}

void devfs_unpublish(device_t* dev) {
    if (dev->self != nullptr) {
        _devfs_remove(dev->self);
        dev->self = nullptr;
    }
    if (dev->link != nullptr) {
        _devfs_remove(dev->link);
        dev->link = nullptr;
    }
}

static zx_status_t devfs_walk(devnode_t** _dn, char* path, char** pathout) {
    devnode_t* dn = *_dn;

again:
    if ((path == nullptr) || (path[0] == 0)) {
        *_dn = dn;
        return ZX_OK;
    }
    char* name = path;
    char* undo = nullptr;
    if ((path = strchr(path, '/')) != nullptr) {
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
        path = nullptr;
    }

    devnode_t* dn = dirdn;
    zx_status_t r = devfs_walk(&dn, path, &path);

    bool describe = flags & ZX_FS_FLAG_DESCRIBE;

    bool no_remote = (dn->device == nullptr) || (dn->device->hrpc == ZX_HANDLE_INVALID);
    bool local_required = devnode_is_local(dn);
    bool local_requested = flags & (ZX_FS_FLAG_NOREMOTE | ZX_FS_FLAG_DIRECTORY);

    if (r == ZX_ERR_NEXT) {
        // We only partially matched -- there's more path to walk.
        if (no_remote || local_required) {
            // No remote to pass this on to.
            r = ZX_ERR_NOT_FOUND;
        } else if (local_requested) {
            // Local requested, but this is remote only.
            r = ZX_ERR_NOT_SUPPORTED;
        } else {
            // There is more path to walk, and this node can accept
            // remote requests.
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
    if (local_requested || local_required) {
        if ((r = iostate_create(dn, h)) < 0) {
            goto fail;
        }
        if (describe) {
            zxrio_describe_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
            msg.status = ZX_OK;
            msg.extra_ptr = (zxrio_node_info_t*)FIDL_ALLOC_PRESENT;
            msg.extra.tag = fuchsia_io_NodeInfoTag_directory;
            zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
        }
        return;
    }

    // Otherwise we will pass the request on to the remote.
    fuchsia_io_DirectoryOpen(dn->device->hrpc, flags, 0, path, strlen(path), h);
}

// Double-check that Open (the only message we forward)
// cannot be mistaken for an internal dev coordinator RPC message
static_assert((fuchsia_io_DirectoryOpenOrdinal & DC_OP_ID_BIT) == 0, "");

static zx_status_t fill_dirent(vdirent_t* de, size_t delen, uint64_t ino,
                               const char* name, size_t len, uint8_t type) {
    size_t sz = sizeof(vdirent_t) + len;

    if (sz > delen || len > NAME_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }
    de->ino = ino;
    de->size = static_cast<uint8_t>(len);
    de->type = type;
    memcpy(de->name, name, len);
    return static_cast<zx_status_t>(sz);
}

static zx_status_t devfs_readdir(devnode_t* dn, uint64_t* _ino, void* data, size_t len) {
    char* ptr = static_cast<char*>(data);
    uint64_t ino = *_ino;

    devnode_t* child;
    list_for_every_entry(&dn->children, child, devnode_t, node) {
        if (child->ino <= ino) {
            continue;
        }
        if (child->device == nullptr) {
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
        auto vdirent = reinterpret_cast<vdirent_t*>(ptr);
        auto child_name_len = static_cast<uint32_t>(strlen(child->name));
        zx_status_t r = fill_dirent(vdirent, len, ino, child->name, child_name_len,
                                    VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    *_ino = ino;
    return static_cast<zx_status_t>(ptr - static_cast<char*>(data));
}

// Helper macros for |devfs_fidl_handler| which make it easier
// avoid typing generated names.

// Decode the incoming request, returning an error and consuming
// all handles on error.
#define DECODE_REQUEST(MSG, METHOD)                                     \
    do {                                                                \
        zx_status_t r;                                                  \
        if ((r = fidl_decode_msg(&fuchsia_io_ ## METHOD ##RequestTable, \
                                 msg, nullptr)) != ZX_OK) {                \
            return r;                                                   \
        }                                                               \
    } while(0);

// Define a variable |request| from the incoming method, of
// the requested type.
#define DEFINE_REQUEST(MSG, METHOD)                     \
    fuchsia_io_ ## METHOD ## Request* request =         \
        (fuchsia_io_ ## METHOD ## Request*) MSG->bytes;


static zx_status_t devfs_fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie) {
    auto ios = static_cast<iostate_t*>(cookie);
    devnode_t* dn = ios->devnode;
    if (dn == nullptr) {
        return ZX_ERR_PEER_CLOSED;
    }

    auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);

    zx_status_t r;
    switch (hdr->ordinal) {
    case fuchsia_io_NodeCloneOrdinal: {
        DECODE_REQUEST(msg, NodeClone);
        DEFINE_REQUEST(msg, NodeClone);
        zx_handle_t h = request->object;
        uint32_t flags = request->flags;
        char path[PATH_MAX];
        path[0] = '\0';
        devfs_open(dn, h, path, flags | ZX_FS_FLAG_NOREMOTE);
        return ZX_OK;
    }
    case fuchsia_io_NodeDescribeOrdinal: {
        DECODE_REQUEST(msg, NodeDescribe);
        fuchsia_io_NodeInfo info;
        memset(&info, 0, sizeof(info));
        info.tag = fuchsia_io_NodeInfoTag_directory;
        return fuchsia_io_NodeDescribe_reply(txn, &info);
    }
    case fuchsia_io_DirectoryOpenOrdinal: {
        DECODE_REQUEST(msg, DirectoryOpen);
        DEFINE_REQUEST(msg, DirectoryOpen);
        uint32_t len = static_cast<uint32_t>(request->path.size);
        char* path = request->path.data;
        zx_handle_t h = request->object;
        uint32_t flags = request->flags;
        if ((len < 1) || (len > 1024)) {
            zx_handle_close(h);
        } else {
            path[len] = '\0';
            devfs_open(dn, h, path, flags);
        }
        return ZX_OK;
    }
    case fuchsia_io_NodeGetAttrOrdinal: {
        DECODE_REQUEST(msg, NodeGetAttr);
        uint32_t mode;
        if (devnode_is_dir(dn)) {
            mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
        } else {
            mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        }

        fuchsia_io_NodeAttributes attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.mode = mode;
        attributes.content_size = 0;
        attributes.link_count = 1;
        attributes.id = dn->ino;
        return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
    }
    case fuchsia_io_DirectoryRewindOrdinal: {
        DECODE_REQUEST(msg, DirectoryRewind);
        ios->readdir_ino = 0;
        return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
    }
    case fuchsia_io_DirectoryReadDirentsOrdinal: {
        DECODE_REQUEST(msg, DirectoryReadDirents);
        DEFINE_REQUEST(msg, DirectoryReadDirents);

        if (request->max_bytes > ZXFIDL_MAX_MSG_BYTES) {
            return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
        }

        uint8_t data[request->max_bytes];
        size_t actual = 0;
        r = devfs_readdir(dn, &ios->readdir_ino, data, request->max_bytes);
        if (r >= 0) {
            actual = r;
            r = ZX_OK;
        }
        return fuchsia_io_DirectoryReadDirents_reply(txn, r, data, actual);
    }
    case fuchsia_io_DirectoryWatchOrdinal: {
        DECODE_REQUEST(msg, DirectoryWatch);
        DEFINE_REQUEST(msg, DirectoryWatch);
        if (request->mask & (~fuchsia_io_WATCH_MASK_ALL) || request->options != 0) {
            zx_handle_close(request->watcher);
            return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_INVALID_ARGS);
        }
        r = devfs_watch(dn, request->watcher, request->mask);
        return fuchsia_io_DirectoryWatch_reply(txn, r);
    }
    case fuchsia_io_DirectoryAdminQueryFilesystemOrdinal: {
        DECODE_REQUEST(msg, DirectoryAdminQueryFilesystem);
        fuchsia_io_FilesystemInfo info;
        memset(&info, 0, sizeof(info));
        strlcpy((char*) info.name, "devfs", fuchsia_io_MAX_FS_NAME_BUFFER);
        return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, ZX_OK, &info);
    }
    case fuchsia_io_NodeIoctlOrdinal: {
        DECODE_REQUEST(msg, NodeIoctl);
        zx_handle_close_many(msg->handles, msg->num_handles);
        return fuchsia_io_NodeIoctl_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr, 0, nullptr, 0);
    }
    }

    // close inbound handles so they do not leak
    zx_handle_close_many(msg->handles, msg->num_handles);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dc_fidl_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    iostate_t* ios = containerof(ph, iostate_t, ph);

    zx_status_t r;
    if (signals & ZX_CHANNEL_READABLE) {
        if ((r = zxfidl_handler(ph->handle, devfs_fidl_handler, ios)) == ZX_OK) {
            return ZX_OK;
        }
    } else if (signals & ZX_CHANNEL_PEER_CLOSED) {
        zxfidl_handler(ZX_HANDLE_INVALID, devfs_fidl_handler, ios);
        r = ZX_ERR_STOP;
    } else {
        printf("dc_fidl_handler: invalid signals %x\n", signals);
        exit(0);
    }

    iostate_destroy(ios);
    return r;
}

static zx_handle_t devfs_root;

zx_handle_t devfs_root_clone() {
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

