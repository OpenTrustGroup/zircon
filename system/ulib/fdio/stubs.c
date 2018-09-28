// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <sys/stat.h>

#include "unistd.h"

// checkfile, checkfileat, and checkfd let us error out if the object
// doesn't exist, which allows the stubs to be a little more 'real'
static int checkfile(const char* path, int err) {
    struct stat s;
    if (stat(path, &s)) {
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    } else {
        return 0;
    }
}

static int checkfileat(int fd, const char* path, int flags, int err) {
    struct stat s;
    if (fstatat(fd, path, &s, flags)) {
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    } else {
        return 0;
    }
}

static int checkfd(int fd, int err) {
    fdio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        errno = EBADF;
        return -1;
    }
    fdio_release(io);
    if (err) {
        errno = err;
        return -1;
    } else {
        return 0;
    }
}

static int checksocket(int fd, int sock_err, int err) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return -1;
    }
    int32_t is_socket = io->ioflag & IOFLAG_SOCKET;
    fdio_release(io);
    if (!is_socket) {
        errno = sock_err;
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    }
    return 0;
}

// not supported by any filesystems yet
__EXPORT
int symlink(const char* existing, const char* new) {
    errno = ENOSYS;
    return -1;
}
__EXPORT
ssize_t readlink(const char* restrict path, char* restrict buf, size_t bufsize) {
    // EINVAL = not symlink
    return checkfile(path, EINVAL);
}

// creating things we don't have plumbing for yet
__EXPORT
int mkfifo(const char *path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
__EXPORT
int mknod(const char* path, mode_t mode, dev_t dev) {
    errno = ENOSYS;
    return -1;
}

// no permissions support yet
__EXPORT
int chown(const char *path, uid_t owner, gid_t group) {
    return checkfile(path, ENOSYS);
}
__EXPORT
int fchown(int fd, uid_t owner, gid_t group) {
    return checkfd(fd, ENOSYS);
}
__EXPORT
int lchown(const char *path, uid_t owner, gid_t group) {
    return checkfile(path, ENOSYS);
}

// no permissions support, but treat rwx bits as don't care rather than error
__EXPORT
int chmod(const char *path, mode_t mode) {
    return checkfile(path, (mode & (~0777)) ? ENOSYS : 0);
}
__EXPORT
int fchmod(int fd, mode_t mode) {
    return checkfd(fd, (mode & (~0777)) ? ENOSYS : 0);
}
__EXPORT
int fchmodat(int fd, const char* path, mode_t mode, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }

    return checkfileat(fd, path, flags, (mode & (~0777)) ? ENOSYS : 0);
}

__EXPORT
int access(const char* path, int mode) {
    return checkfile(path, 0);
}

__EXPORT
void sync(void) {
}

// at the moment our unlink works on all fs objects
__EXPORT
int rmdir(const char* path) {
    return unlink(path);
}

// tty stubbing.
__EXPORT
int ttyname_r(int fd, char* name, size_t size) {
    if (!isatty(fd)) {
        return ENOTTY;
    }

    return checkfd(fd, ENOSYS);
}

__EXPORT
int sendmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

__EXPORT
int recvmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags, struct timespec* timeout) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

__EXPORT
int sockatmark(int fd) {
    // ENOTTY is sic.
    return checksocket(fd, ENOTTY, ENOSYS);
}
