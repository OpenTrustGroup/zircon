// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <zircon/device/device.h>
#include <zircon/device/input.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#define MAX_DESC_LEN 1024

#define INPUT_PATH "/input"
#define DMCTL_PATH "/misc/dmctl"

namespace {

bool usage_eq(const hid::Usage& u1, const hid::Usage& u2) {
    return u1.page == u2.page && u1.usage == u2.usage;
}

// Search the report descriptor for a System Power Down input field within a
// Generic Desktop:System Control collection.
//
// This method assumes the HID descriptor does not contain more than one such field.
zx_status_t FindSystemPowerDown(const hid::DeviceDescriptor* desc,
                                uint8_t* report_id, size_t* bit_offset) {

    const hid::Usage system_control = {
        .page = static_cast<uint16_t>(hid::usage::Page::kGenericDesktop),
        .usage = static_cast<uint32_t>(hid::usage::GenericDesktop::kSystemControl),
    };

    const hid::Usage power_down = {
        .page = static_cast<uint16_t>(hid::usage::Page::kGenericDesktop),
        .usage = static_cast<uint32_t>(hid::usage::GenericDesktop::kSystemPowerDown),
    };

    // Search for the field
    bool found = false;
    for (size_t rpt_idx = 0; rpt_idx < desc->rep_count && !found; ++rpt_idx) {
        const hid::ReportDescriptor& report = desc->report[rpt_idx];
        for (size_t i = 0; i < report.count; ++i) {
            const hid::ReportField& field = report.first_field[i];
            if (field.type != hid::kInput || !usage_eq(field.attr.usage, power_down)) {
                continue;
            }

            const hid::Collection* collection = hid::GetAppCollection(&field);
            if (!collection || !usage_eq(collection->usage, system_control)) {
                continue;
            }

            found = true;
            *report_id = field.report_id;
            break;
        }
    }

    if (!found) {
        return ZX_ERR_NOT_FOUND;
    }

    // Compute the offset of the field.  Since reports may be discontinuous, we
    // have to search from the beginning.
    *bit_offset = 0;
    for (size_t rpt_idx = 0; rpt_idx < desc->rep_count; ++rpt_idx) {
        const hid::ReportDescriptor& report = desc->report[rpt_idx];
        if (report.report_id != *report_id) {
            continue;
        }

        for (size_t i = 0; i < report.count; ++i) {
            const hid::ReportField& field = report.first_field[i];
            if (field.type != hid::kInput) {
                continue;
            }

            *bit_offset += field.attr.bit_sz;

            // Check if we found the field again, and if so return.
            if (!usage_eq(field.attr.usage, power_down)) {
                continue;
            }
            const hid::Collection* collection = hid::GetAppCollection(&field);
            if (!collection || !usage_eq(collection->usage, system_control)) {
                continue;
            }

            // Subtract out the field, since we want its start not its end.
            *bit_offset -= field.attr.bit_sz;
            return ZX_OK;
        }
    }

    // Should be unreachable
    return ZX_ERR_INTERNAL;
}

struct PowerButtonInfo {
    fbl::unique_fd fd;
    uint8_t report_id;
    size_t bit_offset;
    bool has_report_id_byte;
};

static zx_status_t InputDeviceAdded(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }

    fbl::unique_fd fd;
    {
        int raw_fd;
        if ((raw_fd = openat(dirfd, name, O_RDWR)) < 0) {
            return ZX_OK;
        }
        fd.reset(raw_fd);
    }

    // Retrieve and parse the report descriptor
    size_t desc_len = 0;
    if (ioctl_input_get_report_desc_size(fd.get(), &desc_len) < 0) {
        return ZX_OK;
    }
    if (desc_len > MAX_DESC_LEN) {
        return ZX_OK;
    }

    fbl::AllocChecker ac;
    fbl::Array<uint8_t> raw_desc(new (&ac) uint8_t[desc_len](), desc_len);
    if (!ac.check()) {
        return ZX_OK;
    }
    if (ioctl_input_get_report_desc(fd.get(), raw_desc.get(), raw_desc.size()) < 0) {
        return ZX_OK;
    }

    hid::DeviceDescriptor* desc;
    if (hid::ParseReportDescriptor(raw_desc.get(), raw_desc.size(), &desc) != hid::kParseOk) {
        return ZX_OK;
    }
    auto cleanup_desc = fbl::MakeAutoCall([desc]() { hid::FreeDeviceDescriptor(desc); });

    uint8_t report_id;
    size_t bit_offset;
    zx_status_t status = FindSystemPowerDown(desc, &report_id, &bit_offset);
    if (status != ZX_OK) {
        return ZX_OK;
    }

    auto info = reinterpret_cast<PowerButtonInfo*>(cookie);
    info->fd = fbl::move(fd);
    info->report_id = report_id;
    info->bit_offset = bit_offset;
    info->has_report_id_byte = (desc->rep_count > 1 || desc->report[0].report_id != 0);
    return ZX_ERR_STOP;
}

} // namespace

int main(int argc, char**argv) {
    fbl::unique_fd dirfd;
    {
        int fd = open(INPUT_PATH, O_DIRECTORY | O_RDONLY);
        if (fd < 0) {
            printf("pwrbtn-monitor: Failed to open " INPUT_PATH ": %d\n", errno);
            return 1;
        }
        dirfd.reset(fd);
    }

    PowerButtonInfo info;
    zx_status_t status = fdio_watch_directory(dirfd.get(), InputDeviceAdded, ZX_TIME_INFINITE, &info);
    if (status != ZX_ERR_STOP) {
        printf("pwrbtn-monitor: Failed to find power button device\n");
        return 1;
    }
    dirfd.reset();

    input_report_size_t report_size = 0;
    if (ioctl_input_get_max_reportsize(info.fd.get(), &report_size) < 0) {
        printf("pwrbtn-monitor: Failed to to get max report size\n");
        return 1;
    }

    // Double-check the size looks right
    const size_t byte_index = info.has_report_id_byte + info.bit_offset / 8;
    if (report_size <= byte_index) {
        printf("pwrbtn-monitor: Suspicious looking max report size\n");
        return 1;
    }

    fbl::AllocChecker ac;
    fbl::Array<uint8_t> report(new (&ac) uint8_t[report_size](), report_size);
    if (!ac.check()) {
        return 1;
    }

    // Watch the power button device for reports
    while (true) {
        ssize_t r = read(info.fd.get(), report.get(), report.size());
        if (r < 0) {
            printf("pwrbtn-monitor: got read error %zd, bailing\n", r);
            return 1;
        }

        // Ignore reports from different report IDs
        if (info.has_report_id_byte && report[0] != info.report_id) {
            printf("pwrbtn-monitor: input-watcher: wrong id\n");
            continue;
        }

        if (static_cast<size_t>(r) <= byte_index) {
            printf("pwrbtn-monitor: input-watcher: too short\n");
            continue;
        }

        // Check if the power button is pressed, and request a poweroff if so.
        if (report[byte_index] & (1u << (info.bit_offset % 8))) {
            int fd = open(DMCTL_PATH, O_WRONLY);
            if (fd < 0) {
                printf("pwrbtn-monitor: input-watcher: failed to open dmctl\n");
                continue;
            }
            write(fd, "poweroff", strlen("poweroff"));
            close(fd);
        }
    }
}
