// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/private.h>
#include <lib/zx/channel.h>
#include <fbl/auto_call.h>
#include <fbl/type_support.h>
#include <fbl/unique_fd.h>

namespace fzl {

// Helper utility which borrows a file descriptor to allow the caller
// to make access to channel-based calls.
//
// FdioCaller consumes |fd|, but the same |fd| may be re-acquired by
// calling "release()" on the FdioCaller object.
class FdioCaller {
public:
    explicit FdioCaller(fbl::unique_fd fd) :
        fd_(fbl::move(fd)), io_(__fdio_fd_to_io(fd_.get())) {}

    ~FdioCaller() {
        release();
    }

    void reset(fbl::unique_fd fd) {
        release();
        fd_ = fbl::move(fd);
        io_ = __fdio_fd_to_io(fd_.get());
    }

    fbl::unique_fd release() {
        if (io_ != nullptr) {
            __fdio_release(io_);
            io_ = nullptr;
        }
        return fbl::move(fd_);
    }

    explicit operator bool() const {
        return io_ != nullptr;
    }

    // This channel is borrowed, but returned as a zx_handle_t for convenience.
    //
    // It should not be closed.
    // It should not be transferred.
    // It should not be kept alive longer than the FdioCaller object, nor should
    // it be kept alive after FdioCaller.release() is called.
    zx_handle_t borrow_channel() const {
        return __fdio_borrow_channel(io_);
    }

    FdioCaller& operator=(FdioCaller&& o) = delete;
    FdioCaller(FdioCaller&& o) = delete;
    FdioCaller(const FdioCaller&) = delete;
    FdioCaller& operator=(const FdioCaller&) = delete;

private:
    fbl::unique_fd fd_;
    fdio_t* io_;
};

} // namespace fzl
