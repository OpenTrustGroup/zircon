// Copyright 2018 Open Trust Group
// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <vm/vm_object.h>
#include <vm/vm_address_region.h>

#include <lib/user_copy/user_ptr.h>

#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_vmar_allocate(zx_handle_t parent_vmar_handle,
                              zx_vm_option_t options, uint64_t offset, uint64_t size,
                              user_out_handle* child_vmar,
                              user_out_ptr<zx_vaddr_t> child_addr) {

    auto up = ProcessDispatcher::GetCurrent();

    // Compute needed rights from requested mapping protections.
    zx_rights_t vmar_rights = 0u;
    if (options & ZX_VM_CAN_MAP_READ)
        vmar_rights |= ZX_RIGHT_READ;
    if (options & ZX_VM_CAN_MAP_WRITE)
        vmar_rights |= ZX_RIGHT_WRITE;
    if (options & ZX_VM_CAN_MAP_EXECUTE)
        vmar_rights |= ZX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcherWithRights(parent_vmar_handle, vmar_rights, &vmar);
    if (status != ZX_OK)
        return status;

    // Create the new VMAR
    fbl::RefPtr<VmAddressRegionDispatcher> new_vmar;
    zx_rights_t new_rights;
    status = vmar->Allocate(offset, size, options, &new_vmar, &new_rights);
    if (status != ZX_OK)
        return status;

    // Setup a handler to destroy the new VMAR if the syscall is unsuccessful.
    // Note that new_vmar is being passed by value, so a new reference is held
    // there.
    auto cleanup_handler = fbl::MakeAutoCall([new_vmar]() {
        new_vmar->Destroy();
    });

    // Extract the base address before we give away the ref.
    uintptr_t base = new_vmar->vmar()->base();

    // Create a handle and attach the dispatcher to it
    status = child_vmar->make(fbl::move(new_vmar), new_rights);

    if (status == ZX_OK)
        status = child_addr.copy_to_user(base);

    if (status == ZX_OK)
        cleanup_handler.cancel();
    return status;
}

zx_status_t sys_vmar_allocate_old(zx_handle_t parent_vmar_handle,
                                  uint64_t offset, uint64_t size, uint32_t map_flags,
                                  user_out_handle* child_vmar,
                                  user_out_ptr<zx_vaddr_t> child_addr) {
    return sys_vmar_allocate(parent_vmar_handle, map_flags, offset, size, child_vmar, child_addr);
}

zx_status_t sys_vmar_destroy(zx_handle_t vmar_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != ZX_OK)
        return status;

    return vmar->Destroy();
}

zx_status_t sys_vmar_map(zx_handle_t vmar_handle, zx_vm_option_t options,
                         uint64_t vmar_offset, zx_handle_t vmo_handle,
                         uint64_t vmo_offset, uint64_t len,
                         user_out_ptr<zx_vaddr_t> mapped_addr) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the VMAR dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_rights_t vmar_rights;
    zx_status_t status = up->GetDispatcherAndRights(vmar_handle, &vmar, &vmar_rights);
    if (status != ZX_OK)
        return status;

    // lookup the VMO dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_rights_t vmo_rights;
    status = up->GetDispatcherAndRights(vmo_handle, &vmo, &vmo_rights);
    if (status != ZX_OK)
        return status;

    // test to see if we should even be able to map this
    if (!(vmo_rights & ZX_RIGHT_MAP))
        return ZX_ERR_ACCESS_DENIED;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(options))
        return ZX_ERR_INVALID_ARGS;

    bool do_map_range = false;
    if (options & ZX_VM_MAP_RANGE) {
        do_map_range = true;
        options &= ~ZX_VM_MAP_RANGE;
    }

    if (do_map_range && (options & ZX_VM_SPECIFIC_OVERWRITE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Usermode is not allowed to specify these flags on mappings, though we may
    // set them below.
    if (options & (ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_EXECUTE | ZX_VM_MAP_NS)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Permissions allowed by both the VMO and the VMAR
    const bool can_read = (vmo_rights & ZX_RIGHT_READ) && (vmar_rights & ZX_RIGHT_READ);
    const bool can_write = (vmo_rights & ZX_RIGHT_WRITE) && (vmar_rights & ZX_RIGHT_WRITE);
    const bool can_exec = (vmo_rights & ZX_RIGHT_EXECUTE) && (vmar_rights & ZX_RIGHT_EXECUTE);

    // test to see if the requested mapping protections are allowed
    if ((options & ZX_VM_PERM_READ) && !can_read)
        return ZX_ERR_ACCESS_DENIED;
    if ((options & ZX_VM_PERM_WRITE) && !can_write)
        return ZX_ERR_ACCESS_DENIED;
    if ((options & ZX_VM_PERM_EXECUTE) && !can_exec)
        return ZX_ERR_ACCESS_DENIED;

    // If a permission is allowed by both the VMO and the VMAR, add it to the
    // flags for the new mapping, so that the VMO's rights as of now can be used
    // to constrain future permission changes via Protect().
    if (can_read)
        options |= ZX_VM_CAN_MAP_READ;
    if (can_write)
        options |= ZX_VM_CAN_MAP_WRITE;
    if (can_exec)
        options |= ZX_VM_CAN_MAP_EXECUTE;

    // If the VMO owns the permission ZX_RIGHT_MAP_NS, it means
    // that the VMO represents a non-secure memory region.
    if (vmo_rights & ZX_RIGHT_MAP_NS)
    	options |= ZX_VM_MAP_NS;

    fbl::RefPtr<VmMapping> vm_mapping;
    status = vmar->Map(vmar_offset, vmo->vmo(), vmo_offset, len, options, &vm_mapping);
    if (status != ZX_OK)
        return status;

    // Setup a handler to destroy the new mapping if the syscall is unsuccessful.
    auto cleanup_handler = fbl::MakeAutoCall([vm_mapping]() {
        vm_mapping->Destroy();
    });

    if (do_map_range) {
        status = vm_mapping->MapRange(vmo_offset, len, false);
        if (status != ZX_OK) {
            return status;
        }
    }

    status = mapped_addr.copy_to_user(vm_mapping->base());
    if (status != ZX_OK)
        return status;

    cleanup_handler.cancel();
    return ZX_OK;
}

zx_status_t sys_vmar_map_old(zx_handle_t vmar_handle, uint64_t vmar_offset,
                             zx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t len,
                             uint32_t map_flags, user_out_ptr<zx_vaddr_t> mapped_addr) {
    return sys_vmar_map(vmar_handle, map_flags, vmar_offset, vmo_handle, vmo_offset, len, mapped_addr);
}

zx_status_t sys_vmar_unmap(zx_handle_t vmar_handle, zx_vaddr_t addr, uint64_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != ZX_OK)
        return status;

    return vmar->Unmap(addr, len);
}

zx_status_t sys_vmar_protect(zx_handle_t vmar_handle, zx_vm_option_t options, zx_vaddr_t addr, uint64_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    zx_rights_t vmar_rights = 0u;
    if (options & ZX_VM_PERM_READ)
        vmar_rights |= ZX_RIGHT_READ;
    if (options & ZX_VM_PERM_WRITE)
        vmar_rights |= ZX_RIGHT_WRITE;
    if (options & ZX_VM_PERM_EXECUTE)
        vmar_rights |= ZX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcherWithRights(vmar_handle, vmar_rights, &vmar);
    if (status != ZX_OK)
        return status;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(options))
        return ZX_ERR_INVALID_ARGS;

    return vmar->Protect(addr, len, options);
}

zx_status_t sys_vmar_protect_old(zx_handle_t vmar_handle, zx_vaddr_t addr, uint64_t len, uint32_t prot) {
    return sys_vmar_protect(vmar_handle, prot, addr, len);
}
