// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <gzos-shm/resource.h>
#include <zircon/device/sysinfo.h>

constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";

zx_status_t get_shm_resource(zx::resource* resource) {
  fbl::unique_fd fd(open(kSysInfoPath, O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }
  ssize_t n = ioctl_sysinfo_get_ns_shm_resource(
      fd.get(), resource->reset_and_get_address());
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}
