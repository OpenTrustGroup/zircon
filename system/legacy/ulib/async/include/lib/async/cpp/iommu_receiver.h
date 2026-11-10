// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>
#include <zircon/syscalls.h>

namespace async {

// Receives the events an IOMMU reports, by calling |Class::method|.
//
// This kernel delivers no IOMMU events (it has no ARM SMMUv3 IOMMU objects),
// so Begin() reports ZX_ERR_NOT_SUPPORTED and the method is never called.
template <class Class,
          void (Class::*method)(async_t* async, const zx_packet_iommu_event_t* event)>
class IommuReceiverMethod final {
public:
    explicit IommuReceiverMethod(Class* instance) : instance_(instance) {}

    zx_handle_t iommu() const { return iommu_; }
    void set_iommu(zx_handle_t iommu) { iommu_ = iommu; }

    zx_status_t Begin(async_t* async) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t Cancel(async_t* async) { return ZX_ERR_NOT_FOUND; }

private:
    Class* const instance_;
    zx_handle_t iommu_ = ZX_HANDLE_INVALID;
};

} // namespace async
