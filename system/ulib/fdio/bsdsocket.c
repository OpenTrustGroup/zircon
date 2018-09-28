// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

#include <fuchsia/net/c/fidl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/socket.h>
#include <lib/fdio/util.h>

#include "private.h"
#include "unistd.h"

zx_status_t zxsio_accept(fdio_t* io, zx_handle_t* s2);

static zx_status_t fdio_getsockopt(fdio_t* io, int level, int optname,
                                   void* restrict optval,
                                   socklen_t* restrict optlen);

static zx_status_t get_service_handle(const char* path, zx_handle_t* saved,
                                      mtx_t* lock, zx_handle_t* out) {
    zx_status_t r;
    zx_handle_t h0, h1;
    mtx_lock(lock);
    if (*saved == ZX_HANDLE_INVALID) {
        if ((r = zx_channel_create(0, &h0, &h1)) != ZX_OK) {
            mtx_unlock(lock);
            return r;
        }
        if ((r = fdio_service_connect(path, h1)) != ZX_OK) {
            mtx_unlock(lock);
            zx_handle_close(h0);
            return r;
        }
        *saved = h0;
    }
    *out = *saved;
    mtx_unlock(lock);
    return ZX_OK;
}

// This wrapper waits for the service to publish the service handle.
// TODO(ZX-1890): move to a better mechanism when available.
static zx_status_t get_service_with_retries(const char* path, zx_handle_t* saved,
                                            mtx_t* lock, zx_handle_t* out) {
    zx_status_t r;
    unsigned retry = 0;
    while ((r = get_service_handle(path, saved, lock, out)) == ZX_ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return ZX_ERR_NOT_FOUND;
        }
        retry++;
        zx_nanosleep(zx_deadline_after((retry < 8) ? ZX_MSEC(250) : ZX_MSEC(500)));
    }
    return r;
}

static zx_status_t get_dns(zx_handle_t* out) {
    static zx_handle_t saved = ZX_HANDLE_INVALID;
    static mtx_t lock = MTX_INIT;
    return get_service_with_retries("/svc/dns.DNS", &saved, &lock, out);
}

static zx_status_t get_socket_provider(zx_handle_t* out) {
    static zx_handle_t saved = ZX_HANDLE_INVALID;
    static mtx_t lock = MTX_INIT;
    return get_service_with_retries("/svc/fuchsia.net.LegacySocketProvider", &saved, &lock, out);
}

__EXPORT
int socket(int domain, int type, int protocol) {
    fdio_t* io = NULL;
    zx_status_t r;

    zx_handle_t sp;
    r = get_socket_provider(&sp);
    if (r != ZX_OK) {
        return ERRNO(EIO);
    }

    zx_handle_t s = ZX_HANDLE_INVALID;
    int32_t rr = 0;
    r = fuchsia_net_LegacySocketProviderOpenSocket(
        sp, domain, type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC), protocol, &s, &rr);

    if (r != ZX_OK) {
        return ERRNO(EIO);
    }
    if (rr != ZX_OK) {
        return STATUS(rr);
    }

    if (type & SOCK_DGRAM) {
        io = fdio_socket_create_datagram(s, 0);
    } else {
        io = fdio_socket_create_stream(s, 0);
    }

    if (io == NULL) {
        return ERRNO(EIO);
    }

    if (type & SOCK_NONBLOCK) {
        io->ioflag |= IOFLAG_NONBLOCK;
    }

    // TODO(ZX-973): Implement CLOEXEC.
    // if (type & SOCK_CLOEXEC) {
    // }

    int fd;
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
        fdio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

__EXPORT
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXSIO_CONNECT, 0, 0, (void*)addr, len);
    if (r == ZX_ERR_SHOULD_WAIT) {
        if (io->ioflag & IOFLAG_NONBLOCK) {
            io->ioflag |= IOFLAG_SOCKET_CONNECTING;
            fdio_release(io);
            return ERRNO(EINPROGRESS);
        }
        // going to wait for the completion
    } else {
        if (r == ZX_OK) {
            io->ioflag |= IOFLAG_SOCKET_CONNECTED;
        }
        fdio_release(io);
        return STATUS(r);
    }

    // wait for the completion
    uint32_t events = POLLOUT;
    zx_handle_t h;
    zx_signals_t sigs;
    io->ops->wait_begin(io, events, &h, &sigs);
    r = zx_object_wait_one(h, sigs, ZX_TIME_INFINITE, &sigs);
    io->ops->wait_end(io, sigs, &events);
    if (!(events & POLLOUT)) {
        fdio_release(io);
        return ERRNO(EIO);
    }
    if (r < 0) {
        fdio_release(io);
        return ERROR(r);
    }

    // check the result
    zx_status_t status;
    socklen_t status_len = sizeof(status);
    r = fdio_getsockopt(io, SOL_SOCKET, SO_ERROR, &status, &status_len);
    if (r < 0) {
        fdio_release(io);
        return ERRNO(EIO);
    }
    if (status == ZX_OK) {
        io->ioflag |= IOFLAG_SOCKET_CONNECTED;
    }
    fdio_release(io);
    if (status != ZX_OK) {
        return ERRNO(fdio_status_to_errno(status));
    }
    return 0;
}

__EXPORT
int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXSIO_BIND, 0, 0, (void*)addr, len);
    fdio_release(io);
    return STATUS(r);
}

__EXPORT
int listen(int fd, int backlog) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXSIO_LISTEN, 0, 0, &backlog, sizeof(backlog));
    fdio_release(io);
    return STATUS(r);
}

__EXPORT
int accept4(int fd, struct sockaddr* restrict addr, socklen_t* restrict len,
            int flags) {
    if (flags & ~SOCK_NONBLOCK) {
        return ERRNO(EINVAL);
    }

    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_handle_t s2;
    zx_status_t r = zxsio_accept(io, &s2);
    fdio_release(io);
    if (r == ZX_ERR_SHOULD_WAIT) {
        return ERRNO(EWOULDBLOCK);
    } else if (r != ZX_OK) {
        return ERROR(r);
    }

    fdio_t* io2;
    if ((io2 = fdio_socket_create_stream(s2, IOFLAG_SOCKET_CONNECTED)) == NULL) {
        return ERROR(ZX_ERR_NO_RESOURCES);
    }

    if (flags & SOCK_NONBLOCK) {
        io2->ioflag |= IOFLAG_NONBLOCK;
    }

    if (addr != NULL && len != NULL) {
        zxrio_sockaddr_reply_t reply;
        if ((r = io2->ops->misc(io2, ZXSIO_GETPEERNAME, 0,
                                sizeof(zxrio_sockaddr_reply_t), &reply,
                                sizeof(reply))) < 0) {
            io->ops->close(io2);
            fdio_release(io2);
            return ERROR(r);
        }
        socklen_t avail = *len;
        *len = reply.len;
        memcpy(addr, &reply.addr, (avail < reply.len) ? avail : reply.len);
    }

    int fd2;
    if ((fd2 = fdio_bind_to_fd(io2, -1, 0)) < 0) {
        io->ops->close(io2);
        fdio_release(io2);
        return ERRNO(EMFILE);
    }
    return fd2;
}

static int addrinfo_status_to_eai(int32_t status) {
    switch (status) {
    case fuchsia_net_AddrInfoStatus_ok:
        return 0;
    case fuchsia_net_AddrInfoStatus_bad_flags:
        return EAI_BADFLAGS;
    case fuchsia_net_AddrInfoStatus_no_name:
        return EAI_NONAME;
    case fuchsia_net_AddrInfoStatus_again:
        return EAI_AGAIN;
    case fuchsia_net_AddrInfoStatus_fail:
        return EAI_FAIL;
    case fuchsia_net_AddrInfoStatus_no_data:
        return EAI_NONAME;
    case fuchsia_net_AddrInfoStatus_buffer_overflow:
        return EAI_OVERFLOW;
    case fuchsia_net_AddrInfoStatus_system_error:
        return EAI_SYSTEM;
    default:
        // unknown status
        return EAI_SYSTEM;
    }
}

__EXPORT
int getaddrinfo(const char* __restrict node,
                const char* __restrict service,
                const struct addrinfo* __restrict hints,
                struct addrinfo** __restrict res) {
    if ((node == NULL && service == NULL) || res == NULL) {
        errno = EINVAL;
        return EAI_SYSTEM;
    }

    zx_status_t r;
    zx_handle_t sp;
    r = get_socket_provider(&sp);
    if (r != ZX_OK) {
        errno = EIO;
        return EAI_SYSTEM;
    }

    fuchsia_net_String sn_storage, *sn;
    memset(&sn_storage, 0, sizeof(sn_storage));
    sn = &sn_storage;
    if (node == NULL) {
        sn = NULL;
    } else {
        size_t len = strlen(node);
        if (len > sizeof(sn->val)) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(sn->val, node, len);
        sn->len = len;
    }

    fuchsia_net_String ss_storage, *ss;
    memset(&ss_storage, 0, sizeof(ss_storage));
    ss = &ss_storage;
    if (service == NULL) {
        ss = NULL;
    } else {
        size_t len = strlen(service);
        if (len > sizeof(ss->val)) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(ss->val, service, len);
        ss->len = len;
    }

    fuchsia_net_AddrInfoHints ht_storage, *ht;
    memset(&ht_storage, 0, sizeof(ht_storage));
    ht = &ht_storage;
    if (hints == NULL) {
        ht = NULL;
    } else {
        ht->flags = hints->ai_flags;
        ht->family = hints->ai_family;
        ht->sock_type = hints->ai_socktype;
        ht->protocol = hints->ai_protocol;
    }

    fuchsia_net_AddrInfoStatus status = 0;
    int32_t nres = 0;
    fuchsia_net_AddrInfo ai[4];
    r = fuchsia_net_LegacySocketProviderGetAddrInfo(
          sp, sn, ss, ht, &status, &nres, &ai[0], &ai[1], &ai[2], &ai[3]);

    if (r != ZX_OK) {
        errno = fdio_status_to_errno(r);
        return EAI_SYSTEM;
    }
    if (status != fuchsia_net_AddrInfoStatus_ok) {
        int eai = addrinfo_status_to_eai(status);
        if (eai == EAI_SYSTEM) {
            errno = EIO;
            return EAI_SYSTEM;
        }
        return eai;
    }
    if (nres < 0 || nres > 4) {
        errno = EIO;
        return EAI_SYSTEM;
    }

    struct res_entry {
        struct addrinfo ai;
        struct sockaddr_storage addr_storage;
    };
    struct res_entry* entry = calloc(nres, sizeof(struct res_entry));

    for (int i = 0; i < nres; i++) {
        entry[i].ai.ai_flags = ai[i].flags;
        entry[i].ai.ai_family = ai[i].family;
        entry[i].ai.ai_socktype = ai[i].sock_type;
        entry[i].ai.ai_protocol = ai[i].protocol;
        entry[i].ai.ai_addr = (struct sockaddr*)&entry[i].addr_storage;
        entry[i].ai.ai_canonname = NULL; // TODO: support canonname
        if (entry[i].ai.ai_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)entry[i].ai.ai_addr;
            addr->sin_family = AF_INET;
            addr->sin_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin_addr, ai[i].addr.val, ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in);
        } else if (entry[i].ai.ai_family == AF_INET6) {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)entry[i].ai.ai_addr;
            addr->sin6_family = AF_INET6;
            addr->sin6_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin6_addr, ai[i].addr.val, ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in6);
        } else {
            free(entry);
            errno = EIO;
            return EAI_SYSTEM;
        }
    }
    struct addrinfo* next = NULL;
    for (int i = nres - 1; i >= 0; --i) {
        entry[i].ai.ai_next = next;
        next = &entry[i].ai;
    }
    *res = next;

    return 0;
}

__EXPORT
void freeaddrinfo(struct addrinfo* res) {
    free(res);
}

static int getsockaddr(int fd, int op, struct sockaddr* restrict addr,
                       socklen_t* restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zxrio_sockaddr_reply_t reply;
    zx_status_t r = io->ops->misc(io, op, 0, sizeof(zxrio_sockaddr_reply_t),
                                  &reply, sizeof(reply));
    fdio_release(io);

    if (r < 0) {
        return ERROR(r);
    }

    socklen_t avail = *len;
    *len = reply.len;
    memcpy(addr, &reply.addr, (avail < reply.len) ? avail : reply.len);

    return 0;
}

__EXPORT
int getsockname(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    return getsockaddr(fd, ZXSIO_GETSOCKNAME, addr, len);
}

__EXPORT
int getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    return getsockaddr(fd, ZXSIO_GETPEERNAME, addr, len);
}

static zx_status_t fdio_getsockopt(fdio_t* io, int level, int optname,
                                   void* restrict optval, socklen_t* restrict optlen) {
    if (optval == NULL || optlen == NULL) {
        return ERRNO(EINVAL);
    }

    zxrio_sockopt_req_reply_t req_reply;
    req_reply.level = level;
    req_reply.optname = optname;
    zx_status_t r = io->ops->misc(io, ZXSIO_GETSOCKOPT, 0,
                                  sizeof(zxrio_sockopt_req_reply_t),
                                  &req_reply, sizeof(req_reply));
    if (r < 0) {
        return r;
    }
    socklen_t avail = *optlen;
    *optlen = req_reply.optlen;
    memcpy(optval, req_reply.optval,
           (avail < req_reply.optlen) ? avail : req_reply.optlen);

    return ZX_OK;
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* restrict optval,
               socklen_t* restrict optlen) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    if (level == SOL_SOCKET && optname == SO_ERROR) {
        if (optval == NULL || optlen == NULL || *optlen < sizeof(int)) {
            r = ZX_ERR_INVALID_ARGS;
        } else {
            zx_status_t status;
            socklen_t status_len = sizeof(status);
            r = fdio_getsockopt(io, SOL_SOCKET, SO_ERROR, &status, &status_len);
            if (r == ZX_OK) {
                int errno_ = 0;
                if (status != ZX_OK) {
                    errno_ = fdio_status_to_errno(status);
                }
                *(int*)optval = errno_;
                *optlen = sizeof(int);
            }
        }
    } else {
        r = fdio_getsockopt(io, level, optname, optval, optlen);
    }
    fdio_release(io);

    return STATUS(r);
}

__EXPORT
int setsockopt(int fd, int level, int optname, const void* optval,
               socklen_t optlen) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zxrio_sockopt_req_reply_t req;
    req.level = level;
    req.optname = optname;
    if (optlen > sizeof(req.optval)) {
        fdio_release(io);
        return ERRNO(EINVAL);
    }
    memcpy(req.optval, optval, optlen);
    req.optlen = optlen;
    zx_status_t r = io->ops->misc(io, ZXSIO_SETSOCKOPT, 0, 0, &req,
                                  sizeof(req));
    fdio_release(io);
    return STATUS(r);
}
