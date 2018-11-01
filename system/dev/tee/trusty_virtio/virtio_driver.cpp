// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backends/remoteproc.h"

#include <limits.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_call.h>
#include <fbl/ref_counted.h>
#include <fbl/vector.h>
#include <lib/zx/bti.h>
#include <virtio/trusty.h>
#include <zircon/status.h>

#include "controller.h"
#include "shared_memory.h"
#include "smc.h"
#include "trace.h"
#include "trusty_vdev.h"

class ResourceTableIterator {
public:
    struct TableEntry {
        uint32_t size;
        fw_rsc_hdr hdr;
    };

    ResourceTableIterator(trusty_virtio::SharedMemory* shm)
        : shm_(shm) {
        auto rsc_table = shm_->as<resource_table>(0);
        number_ = rsc_table->num;
        offset_ = sizeof(resource_table);
    }

    operator bool() const {
        return number_ > 0;
    }

    TableEntry& operator*() const {
        auto entry = shm_->as<TableEntry>(offset_);
        return *entry;
    }

    TableEntry* operator->() const {
        return &**this;
    }

    ResourceTableIterator& operator++() {
        number_--;

        auto entry = shm_->as<TableEntry>(offset_);
        offset_ += (sizeof(uint32_t) + entry->size);
        return *this;
    }

    const ResourceTableIterator& begin() {
        return *this;
    }

    ResourceTableIterator end() {
        return ResourceTableIterator();
    }

private:
    ResourceTableIterator()
        : shm_(nullptr), offset_(0), number_(0) {}
    trusty_virtio::SharedMemory* shm_;
    uint64_t offset_;
    uint64_t next_offset_;
    uint32_t number_;
};

// TODO(sy): bus_device will be overwritten to 0 if we don't lower optimization level.
// After investigation, it looks like a compiler bug. Let's revisit it after next
// compiler upgradation.
extern "C" zx_status_t __attribute__((optimize("O0"))) virtio_trusty_bind(void* ctx, zx_device_t* bus_device) {
    auto controller = trusty_virtio::Controller::Instance();

    zx_status_t status;
    fbl::unique_ptr<trusty_virtio::SharedMemory> table;
    status = controller->shm_pool().Allocate(PAGE_SIZE, &table);
    if (status != ZX_OK) {
        return status;
    }

    trusty_virtio::NonSecurePageInfo pi(table->paddr());
    zx_smc_result_t get_descr_result;
    status = controller->monitor_std_call(SMC_SC_VIRTIO_GET_DESCR, &get_descr_result,
                                          pi.low(), pi.high(), static_cast<uint32_t>(table->size()));
    if (status != ZX_OK) {
        return status;
    }

    fbl::Vector<fbl::unique_ptr<virtio::Device>> devices;
    auto remove_devices = fbl::MakeAutoCall([&devices] {
        for (auto& device : devices) {
            device->Unbind();
        }
        devices.reset();
    });

    ResourceTableIterator entries(table.get());
    for (auto& entry : entries) {
        switch (entry.hdr.type) {
        case RSC_VDEV: {
            auto descr = reinterpret_cast<virtio_trusty_vdev_descr_t*>(&entry.hdr);
            TRACEF("Probed trusty vdev '%s', id:%d\n", descr->config.dev_name, descr->vdev.id);

            fbl::unique_ptr<virtio::Backend> backend =
                fbl::make_unique<trusty_virtio::RemoteProc>(descr);
            if (!backend) {
                TRACEF("Failed to alloc RemoteProc\n");
                return ZX_ERR_NO_MEMORY;
            }

            auto trusty_vdev = fbl::make_unique<trusty_virtio::TrustyVirtioDevice>(
                bus_device, zx::bti(), fbl::move(backend));
            if (!trusty_vdev) {
                TRACEF("Failed to alloc TrustyVirtioDevice\n");
                return ZX_ERR_NO_MEMORY;
            }

            status = trusty_vdev->Init();
            if (status != ZX_OK) {
                TRACEF("Failed to initialize TrustyVirtioDevice, status=%s\n",
                       zx_status_get_string(status));
                return status;
            }

            devices.push_back(fbl::move(trusty_vdev));
            break;
        }
        default:
            TRACEF("Bad resource table entry: type: %d\n", entry.hdr.type);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    uint32_t table_size = static_cast<uint32_t>(get_descr_result.arg0);
    status = controller->monitor_std_call(SMC_SC_VIRTIO_START, nullptr, pi.low(), pi.high(),
                                          table_size);
    if (status != ZX_OK) {
        TRACEF("Failed to Start Virtio, status=%s\n", zx_status_get_string(status));
        return status;
    }

    remove_devices.cancel();
    for (auto& device : devices) {
        __UNUSED auto ptr = device.release();
    }

    return ZX_OK;
}
