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

#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fdio/debug.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>
#include <fdio/socket.h>

#include "private.h"
#include "unistd.h"

zx_status_t zxsio_accept(fdio_t* io, zx_handle_t* s2);

static zx_status_t fdio_getsockopt(fdio_t* io, int level, int optname,
                                   void* restrict optval,
                                   socklen_t* restrict optlen);

static mtx_t netstack_lock = MTX_INIT;
static int netstack = INT_MIN;

int get_netstack(void) {
    mtx_lock(&netstack_lock);
    if (netstack == INT_MIN)
        netstack = open("/svc/net.Netstack", O_PIPELINE | O_RDWR);
    int result = netstack;
    mtx_unlock(&netstack_lock);
    return result;
}

int socket(int domain, int type, int protocol) {

    fdio_t* io = NULL;
    zx_status_t r;

    // SOCK_NONBLOCK and SOCK_CLOEXEC in type are handled locally rather than
    // remotely so do not include them in path.
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%d/%d/%d", ZXRIO_SOCKET_DIR_SOCKET,
                     domain, type & ~(SOCK_NONBLOCK|SOCK_CLOEXEC), protocol);
    if (n < 0 || n >= (int)sizeof(path)) {
        return ERRNO(EINVAL);
    }

    // Wait for the the network stack to publish the socket device
    // if necessary.
    // TODO: move to a better mechanism when available.
    unsigned retry = 0;
    while ((r = __fdio_open_at(&io, get_netstack(), path, 0, 0)) == ZX_ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return ERRNO(EIO);
        }
        retry++;
        zx_nanosleep(zx_deadline_after((retry < 8) ? ZX_MSEC(250) : ZX_MSEC(500)));
    }
    if (r < 0) {
        return ERROR(r);
    }
    if (type & SOCK_STREAM) {
        fdio_socket_set_stream_ops(io);
    } else if (type & SOCK_DGRAM) {
        fdio_socket_set_dgram_ops(io);
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

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXRIO_CONNECT, 0, 0, (void*)addr, len);
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

int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXRIO_BIND, 0, 0, (void*)addr, len);
    fdio_release(io);
    return STATUS(r);
}

int listen(int fd, int backlog) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    r = io->ops->misc(io, ZXRIO_LISTEN, 0, 0, &backlog, sizeof(backlog));
    fdio_release(io);
    return STATUS(r);
}

int accept4(int fd, struct sockaddr* restrict addr, socklen_t* restrict len,
            int flags) {
    if (flags & ~SOCK_NONBLOCK) {
        return ERRNO(EINVAL);
    }

    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

#if WITH_NEW_SOCKET
    zx_handle_t s2;
    zx_status_t r = zxsio_accept(io, &s2);
    if (r == ZX_ERR_SHOULD_WAIT) {
        return ERRNO(EWOULDBLOCK);
    } else if (r != ZX_OK) {
        fdio_release(io);
        return ERROR(r);
    }

    fdio_t* io2;
    if ((io2 = fdio_socket_create(s2, IOFLAG_SOCKET_CONNECTED)) == NULL) {
        return ERROR(ZX_ERR_NO_RESOURCES);
    }
#else
    fdio_t* io2;
    zx_status_t r;
    for (;;) {
        r = io->ops->open(io, ZXRIO_SOCKET_DIR_ACCEPT, ZX_FS_FLAG_DESCRIBE, 0, &io2);
        if (r == ZX_ERR_SHOULD_WAIT) {
            if (io->ioflag & IOFLAG_NONBLOCK) {
                fdio_release(io);
                return ERRNO(EWOULDBLOCK);
            }
            // wait for an incoming connection
            uint32_t events = POLLIN;
            zx_handle_t h;
            zx_signals_t sigs;
            io->ops->wait_begin(io, events, &h, &sigs);
            r = zx_object_wait_one(h, sigs, ZX_TIME_INFINITE, &sigs);
            io->ops->wait_end(io, sigs, &events);
            if (!(events & POLLIN)) {
                fdio_release(io);
                return ERRNO(EIO);
            }
            continue;
        } else if (r == ZX_OK) {
            break;
        }
        fdio_release(io);
        return ERROR(r);
    }
#endif
    fdio_release(io);

    fdio_socket_set_stream_ops(io2);
    io2->ioflag |= IOFLAG_SOCKET_CONNECTED;

    if (flags & SOCK_NONBLOCK) {
        io2->ioflag |= IOFLAG_NONBLOCK;
    }

    if (addr != NULL && len != NULL) {
        zxrio_sockaddr_reply_t reply;
        if ((r = io2->ops->misc(io2, ZXRIO_GETPEERNAME, 0,
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

int getaddrinfo(const char* __restrict node,
                const char* __restrict service,
                const struct addrinfo* __restrict hints,
                struct addrinfo** __restrict res) {
    fdio_t* io = NULL;
    zx_status_t r;

    if ((node == NULL && service == NULL) || res == NULL) {
        errno = EINVAL;
        return EAI_SYSTEM;
    }
    // Wait for the the network stack to publish the socket device
    // if necessary.
    // TODO: move to a better mechanism when available.
    unsigned retry = 0;
    while ((r = __fdio_open_at(&io, get_netstack(), ZXRIO_SOCKET_DIR_NONE,
                               0, 0)) == ZX_ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return EAI_AGAIN;
        }
        retry++;
        zx_nanosleep(zx_deadline_after((retry < 8) ? ZX_MSEC(250) : ZX_MSEC(500)));
    }
    if (r < 0) {
        errno = fdio_status_to_errno(r);
        return EAI_SYSTEM;
    }

    static_assert(sizeof(zxrio_gai_req_reply_t) <= FDIO_CHUNK_SIZE,
                  "this type should be no larger than FDIO_CHUNK_SIZE");

    zxrio_gai_req_reply_t gai;

    gai.req.node_is_null = (node == NULL) ? 1 : 0;
    gai.req.service_is_null = (service == NULL) ? 1 : 0;
    gai.req.hints_is_null = (hints == NULL) ? 1 : 0;
    if (node) {
        strncpy(gai.req.node, node, ZXRIO_GAI_REQ_NODE_MAXLEN);
        gai.req.node[ZXRIO_GAI_REQ_NODE_MAXLEN-1] = '\0';
    }
    if (service) {
        strncpy(gai.req.service, service, ZXRIO_GAI_REQ_SERVICE_MAXLEN);
        gai.req.service[ZXRIO_GAI_REQ_SERVICE_MAXLEN-1] = '\0';
    }
    if (hints) {
        if (hints->ai_addrlen != 0 || hints->ai_addr != NULL ||
            hints->ai_canonname != NULL || hints->ai_next != NULL) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(&gai.req.hints, hints, sizeof(struct addrinfo));
    }

    r = io->ops->misc(io, ZXRIO_GETADDRINFO, 0, sizeof(zxrio_gai_reply_t),
                      &gai, sizeof(gai));
    io->ops->close(io);
    fdio_release(io);

    if (r < 0) {
        errno = fdio_status_to_errno(r);
        return EAI_SYSTEM;
    }
    if (gai.reply.retval == 0) {
        // alloc the memory for the out param
        zxrio_gai_reply_t* reply = calloc(1, sizeof(*reply));
        // copy the reply
        memcpy(reply, &gai.reply, sizeof(*reply));

        // link all entries in the reply
        struct addrinfo *next = NULL;
        for (int i = reply->nres - 1; i >= 0; --i) {
            // adjust ai_addr to point the new address if not NULL
            if (reply->res[i].ai.ai_addr != NULL) {
                reply->res[i].ai.ai_addr =
                    (struct sockaddr*)&reply->res[i].addr;
            }
            reply->res[i].ai.ai_next = next;
            next = &reply->res[i].ai;
        }
        // the top of the reply must be the first addrinfo in the list
        assert(next == (struct addrinfo*)reply);
        *res = next;
    }
    return gai.reply.retval;
}

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

int getsockname(int fd, struct sockaddr* restrict addr, socklen_t* restrict len)
{
    return getsockaddr(fd, ZXRIO_GETSOCKNAME, addr, len);
}

int getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len)
{
    return getsockaddr(fd, ZXRIO_GETPEERNAME, addr, len);
}

static zx_status_t fdio_getsockopt(fdio_t* io, int level, int optname,
                            void* restrict optval, socklen_t* restrict optlen) {
    if (optval == NULL || optlen == NULL) {
        return ERRNO(EINVAL);
    }

    zxrio_sockopt_req_reply_t req_reply;
    req_reply.level = level;
    req_reply.optname = optname;
    zx_status_t r = io->ops->misc(io, ZXRIO_GETSOCKOPT, 0,
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
    zx_status_t r = io->ops->misc(io, ZXRIO_SETSOCKOPT, 0, 0, &req,
                                  sizeof(req));
    fdio_release(io);
    return STATUS(r);
}

static ssize_t fdio_recvmsg(fdio_t* io, struct msghdr* msg, int flags) {
    zx_status_t r = io->ops->recvmsg(io, msg, flags);
    // TODO(ZX-974): audit error codes
    if (r == ZX_ERR_WRONG_TYPE)
        return ERRNO(ENOTSOCK);
    else if (r == ZX_ERR_BAD_STATE)
        return ERRNO(ENOTCONN);
    else if (r == ZX_ERR_ALREADY_EXISTS)
        return ERRNO(EISCONN);
    return STATUS(r);
}
