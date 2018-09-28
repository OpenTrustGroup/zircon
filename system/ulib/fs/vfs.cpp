// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/watcher.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fs/connection.h>
#include <fs/remote.h>
#include <threads.h>
#include <zircon/assert.h>
#include <lib/zx/event.h>
#include <lib/zx/process.h>
#endif

#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

// #define DEBUG_PRINTF
#ifdef DEBUG_PRINTF
#define xprintf(args...) fprintf(stderr, args)
#else
#define xprintf(args...)
#endif

namespace fs {
namespace {

// Trim a name before sending it to internal filesystem functions.
// Trailing '/' characters imply that the name must refer to a directory.
zx_status_t vfs_name_trim(fbl::StringPiece name, fbl::StringPiece* name_out,
                          bool* dir_out) {
    size_t len = name.length();
    bool is_dir = false;
    while ((len > 0) && name[len - 1] == '/') {
        len--;
        is_dir = true;
    }

    // 'name' should not contain paths consisting of exclusively '/' characters.
    if (len == 0) {
        return ZX_ERR_INVALID_ARGS;
    } else if (len > NAME_MAX) {
        return ZX_ERR_BAD_PATH;
    }

    name_out->set(name.data(), len);
    *dir_out = is_dir;
    return ZX_OK;
}

zx_status_t vfs_lookup(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                       fbl::StringPiece name) {
    if (name == "..") {
        return ZX_ERR_INVALID_ARGS;
    } else if (name == ".") {
        *out = fbl::move(vn);
        return ZX_OK;
    }
    return vn->Lookup(out, name);
}

// Validate open flags as much as they can be validated
// independently of the target node.
zx_status_t vfs_prevalidate_flags(uint32_t flags) {
    if (!(flags & ZX_FS_RIGHT_WRITABLE)) {
        if (flags & ZX_FS_FLAG_TRUNCATE) {
            return ZX_ERR_INVALID_ARGS;
        }
    } else if (!(flags & ZX_FS_RIGHTS)) {
        if (!IsPathOnly(flags)) {
            return ZX_ERR_INVALID_ARGS;
        }
    }
    return ZX_OK;
}

} // namespace

#ifdef __Fuchsia__

bool RemoteContainer::IsRemote() const {
    return remote_.is_valid();
}

zx::channel RemoteContainer::DetachRemote() {
    return fbl::move(remote_);
}

zx_handle_t RemoteContainer::GetRemote() const {
    return remote_.get();
}

void RemoteContainer::SetRemote(zx::channel remote) {
    ZX_DEBUG_ASSERT(!remote_.is_valid());
    remote_ = fbl::move(remote);
}

#endif

Vfs::Vfs() = default;
Vfs::~Vfs() = default;

#ifdef __Fuchsia__
Vfs::Vfs(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}
#endif

zx_status_t Vfs::Open(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                      fbl::StringPiece path, fbl::StringPiece* out_path, uint32_t flags,
                      uint32_t mode) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    return OpenLocked(fbl::move(vndir), out, path, out_path, flags, mode);
}

zx_status_t Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                            fbl::StringPiece path, fbl::StringPiece* out_path,
                            uint32_t flags, uint32_t mode) {
    xprintf("VfsOpen: path='%s' flags=%d\n", path.begin(), flags);
    zx_status_t r;
    if ((r = vfs_prevalidate_flags(flags)) != ZX_OK) {
        return r;
    }
    if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
#ifdef __Fuchsia__
    if (vndir->IsRemote()) {
        // remote filesystem, return handle and path through to caller
        *out = fbl::move(vndir);
        *out_path = path;
        return ZX_OK;
    }
#endif

    fbl::RefPtr<Vnode> vn;

    bool must_be_dir = false;
    if ((r = vfs_name_trim(path, &path, &must_be_dir)) != ZX_OK) {
        return r;
    } else if (path == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if (flags & ZX_FS_FLAG_CREATE) {
        if (must_be_dir && !S_ISDIR(mode)) {
            return ZX_ERR_INVALID_ARGS;
        } else if (path == ".") {
            return ZX_ERR_INVALID_ARGS;
        } else if (ReadonlyLocked()) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = vndir->Create(&vn, path, mode)) < 0) {
            if ((r == ZX_ERR_ALREADY_EXISTS) && (!(flags & ZX_FS_FLAG_EXCLUSIVE))) {
                goto try_open;
            }
            if (r == ZX_ERR_NOT_SUPPORTED) {
                // filesystem may not support create (like devfs)
                // in which case we should still try to open() the file
                goto try_open;
            }
            return r;
        }
#ifdef __Fuchsia__
        vndir->Notify(path, fuchsia_io_WATCH_EVENT_ADDED);
#endif
    } else {
    try_open:
        r = vfs_lookup(fbl::move(vndir), &vn, path);
        if (r < 0) {
            return r;
        }
#ifdef __Fuchsia__
        if (!(flags & ZX_FS_FLAG_NOREMOTE) && vn->IsRemote()) {
            // Opening a mount point: Traverse across remote.
            *out_path = ".";
            *out = fbl::move(vn);
            return ZX_OK;
        }

        flags |= (must_be_dir ? ZX_FS_FLAG_DIRECTORY : 0);
#endif
        if (ReadonlyLocked() && IsWritable(flags)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = vn->ValidateFlags(flags)) != ZX_OK) {
            return r;
        }
        // VNODE_REF_ONLY requests that we don't actually open the underlying
        // Vnode.
        if (!IsPathOnly(flags)) {
            if ((r = OpenVnode(flags, &vn)) != ZX_OK) {
                return r;
            }
            if ((flags & ZX_FS_FLAG_TRUNCATE) && ((r = vn->Truncate(0)) < 0)) {
                vn->Close();
                return r;
            }
        }
    }
    xprintf("VfsOpen: vn=%p\n", vn.get());
    *out_path = "";
    *out = vn;
    return ZX_OK;
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path) {
    bool must_be_dir;
    zx_status_t r;
    if ((r = vfs_name_trim(path, &path, &must_be_dir)) != ZX_OK) {
        return r;
    } else if (path == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (path == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    {
#ifdef __Fuchsia__
        fbl::AutoLock lock(&vfs_lock_);
#endif
        if (ReadonlyLocked()) {
            r = ZX_ERR_ACCESS_DENIED;
        } else {
            r = vndir->Unlink(path, must_be_dir);
        }
    }
    if (r != ZX_OK) {
        return r;
    }
#ifdef __Fuchsia__
    vndir->Notify(path, fuchsia_io_WATCH_EVENT_REMOVED);
#endif
    return ZX_OK;
}

#ifdef __Fuchsia__

#define TOKEN_RIGHTS (ZX_RIGHTS_BASIC)

void Vfs::TokenDiscard(zx::event ios_token) {
    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token) {
        // The token is cleared here to prevent the following race condition:
        // 1) Open
        // 2) GetToken
        // 3) Close + Release Vnode
        // 4) Use token handle to access defunct vnode (or a different vnode,
        //    if the memory for it is reallocated).
        //
        // By cleared the token cookie, any remaining handles to the event will
        // be ignored by the filesystem server.
        ios_token.set_cookie(*zx::process::self(), 0);
    }
}

zx_status_t Vfs::VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token,
                              zx::event* out) {
    uint64_t vnode_cookie = reinterpret_cast<uint64_t>(vn.get());
    zx_status_t r;

    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token->is_valid()) {
        // Token has already been set for this iostate
        if ((r = ios_token->duplicate(TOKEN_RIGHTS, out) != ZX_OK)) {
            return r;
        }
        return ZX_OK;
    }

    zx::event new_token;
    zx::event new_ios_token;
    if ((r = zx::event::create(0, &new_ios_token)) != ZX_OK) {
        return r;
    } else if ((r = new_ios_token.duplicate(TOKEN_RIGHTS, &new_token) != ZX_OK)) {
        return r;
    } else if ((r = new_ios_token.set_cookie(*zx::process::self(), vnode_cookie)) != ZX_OK) {
        return r;
    }
    *ios_token = fbl::move(new_ios_token);
    *out = fbl::move(new_token);
    return ZX_OK;
}

zx_status_t Vfs::TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) {
    uint64_t vcookie;
    zx_status_t r;
    if ((r = token.get_cookie(*zx::process::self(), &vcookie)) < 0) {
        // TODO(smklein): Return a more specific error code for "token not from this server"
        return ZX_ERR_INVALID_ARGS;
    }

    if (vcookie == 0) {
        // Client closed the channel associated with the token
        return ZX_ERR_INVALID_ARGS;
    }

    *out = fbl::RefPtr<fs::Vnode>(reinterpret_cast<fs::Vnode*>(vcookie));
    return ZX_OK;
}

zx_status_t Vfs::Rename(zx::event token, fbl::RefPtr<Vnode> oldparent,
                        fbl::StringPiece oldStr, fbl::StringPiece newStr) {
    // Local filesystem
    bool old_must_be_dir;
    bool new_must_be_dir;
    zx_status_t r;
    if ((r = vfs_name_trim(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
        return r;
    } else if (oldStr == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (oldStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if ((r = vfs_name_trim(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
        return r;
    } else if (newStr == "." || newStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<fs::Vnode> newparent;
    {
        fbl::AutoLock lock(&vfs_lock_);
        if (ReadonlyLocked()) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = TokenToVnode(fbl::move(token), &newparent)) != ZX_OK) {
            return r;
        }

        r = oldparent->Rename(newparent, oldStr, newStr, old_must_be_dir,
                              new_must_be_dir);
    }
    if (r != ZX_OK) {
        return r;
    }
    oldparent->Notify(oldStr, fuchsia_io_WATCH_EVENT_REMOVED);
    newparent->Notify(newStr, fuchsia_io_WATCH_EVENT_ADDED);
    return ZX_OK;
}

zx_status_t Vfs::Readdir(Vnode* vn, vdircookie_t* cookie,
                         void* dirents, size_t len, size_t* out_actual) {
    fbl::AutoLock lock(&vfs_lock_);
    return vn->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Vfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent,
                      fbl::StringPiece oldStr, fbl::StringPiece newStr) {
    fbl::AutoLock lock(&vfs_lock_);
    fbl::RefPtr<fs::Vnode> newparent;
    zx_status_t r;
    if ((r = TokenToVnode(fbl::move(token), &newparent)) != ZX_OK) {
        return r;
    }
    // Local filesystem
    bool old_must_be_dir;
    bool new_must_be_dir;
    if (ReadonlyLocked()) {
        return ZX_ERR_ACCESS_DENIED;
    } else if ((r = vfs_name_trim(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
        return r;
    } else if (old_must_be_dir) {
        return ZX_ERR_NOT_DIR;
    } else if (oldStr == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (oldStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if ((r = vfs_name_trim(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
        return r;
    } else if (new_must_be_dir) {
        return ZX_ERR_NOT_DIR;
    } else if (newStr == "." || newStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    // Look up the target vnode
    fbl::RefPtr<Vnode> target;
    if ((r = oldparent->Lookup(&target, oldStr)) < 0) {
        return r;
    }
    r = newparent->Link(newStr, target);
    if (r != ZX_OK) {
        return r;
    }
    newparent->Notify(newStr, fuchsia_io_WATCH_EVENT_ADDED);
    return ZX_OK;
}

zx_status_t Vfs::ServeConnection(fbl::unique_ptr<Connection> connection) {
    ZX_DEBUG_ASSERT(connection);

    zx_status_t status = connection->Serve();
    if (status == ZX_OK) {
        RegisterConnection(fbl::move(connection));
    }
    return status;
}

void Vfs::OnConnectionClosedRemotely(Connection* connection) {
    ZX_DEBUG_ASSERT(connection);

    UnregisterConnection(connection);
}

zx_status_t Vfs::ServeDirectory(fbl::RefPtr<fs::Vnode> vn, zx::channel channel) {
    uint32_t flags = ZX_FS_FLAG_DIRECTORY;
    zx_status_t r;
    if ((r = vn->ValidateFlags(flags)) != ZX_OK) {
        return r;
    } else if ((r = OpenVnode(flags, &vn)) != ZX_OK) {
        return r;
    }

    // Tell the calling process that we've mounted the directory.
    r = channel.signal_peer(0, ZX_USER_SIGNAL_0);
    // ZX_ERR_PEER_CLOSED is ok because the channel may still be readable.
    if (r != ZX_OK && r != ZX_ERR_PEER_CLOSED) {
        return r;
    }

    return vn->Serve(this, fbl::move(channel), ZX_FS_RIGHT_ADMIN);
}

#endif // ifdef __Fuchsia__

void Vfs::SetReadonly(bool value) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    readonly_ = value;
}

zx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out_vn,
                      fbl::StringPiece path, fbl::StringPiece* out_path) {
    zx_status_t r;
    while (!path.empty() && path[path.length() - 1] == '/') {
        // Discard extra trailing '/' characters.
        path.set(path.data(), path.length() - 1);
    }

    for (;;) {
        while (!path.empty() && path[0] == '/') {
            // Discard extra leading '/' characters.
            path.set(&path[1], path.length() - 1);
        }
        if (path.empty()) {
            // Convert empty initial path of final path segment to ".".
            path.set(".", 1);
        }
#ifdef __Fuchsia__
        if (vn->IsRemote()) {
            // Remote filesystem mount, caller must resolve.
            *out_vn = fbl::move(vn);
            *out_path = fbl::move(path);
            return ZX_OK;
        }
#endif

        // Look for the next '/' separated path component.
        const char* next_path = reinterpret_cast<const char*>(
                memchr(path.data(), '/', path.length()));
        if (next_path == nullptr) {
            // Final path segment.
            *out_vn = vn;
            *out_path = fbl::move(path);
            return ZX_OK;
        }

        // Path has at least one additional segment.
        fbl::StringPiece component(path.data(), next_path - path.data());
        if ((r = vfs_lookup(fbl::move(vn), &vn, component)) != ZX_OK) {
            return r;
        }
        // Traverse to the next segment.
        path.set(next_path + 1, path.length() - (component.length() + 1));
    }
}

} // namespace fs
