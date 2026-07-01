// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vm_object_dispatcher.h>

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <zircon/rights.h>

#include <fbl/alloc_checker.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <trace.h>

#define LOCAL_TRACE 0

zx_status_t VmObjectDispatcher::parse_create_syscall_flags(uint32_t flags, uint32_t* out_flags) {
    uint32_t res = 0;
    // [PORT-BLOCKED] ZX_VMO_NON_RESIZABLE missing in 2018.
    /*
    if (flags & ZX_VMO_NON_RESIZABLE) {
        res |= VMM_FLAG_VMO_RESIZABLE;
        flags &= ~ZX_VMO_NON_RESIZABLE;
    }
    */
    // 2018 uses the modern opt-in ZX_VMO_RESIZABLE flag instead of the
    // NON_RESIZABLE form snapshotted above. Mirroring upstream, translate it into
    // the parsed options. 2018 has no VmObjectPaged::kResizable constant, so the
    // ABI bit is carried through directly and read by VmObjectPaged::CreateExternal.
    if (flags & ZX_VMO_RESIZABLE) {
        res |= ZX_VMO_RESIZABLE;
        flags &= ~ZX_VMO_RESIZABLE;
    }
    if (flags) {
        return ZX_ERR_INVALID_ARGS;
    }

    *out_flags = res;
    return ZX_OK;
}

zx_status_t VmObjectDispatcher::Create(fbl::RefPtr<VmObject> vmo,
                                       fbl::RefPtr<Dispatcher>* dispatcher,
                                       zx_rights_t* rights) {
    return Create(fbl::move(vmo), 0, dispatcher, rights);
}

zx_status_t VmObjectDispatcher::Create(fbl::RefPtr<VmObject> vmo,
                                       zx_koid_t pager_koid,
                                       fbl::RefPtr<Dispatcher>* dispatcher,
                                       zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(fbl::move(vmo), pager_koid);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    disp->vmo()->set_user_id(disp->get_koid());
    *rights = ZX_DEFAULT_VMO_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

VmObjectDispatcher::VmObjectDispatcher(fbl::RefPtr<VmObject> vmo, zx_koid_t pager_koid)
    : SoloDispatcher(ZX_VMO_ZERO_CHILDREN), vmo_(vmo), pager_koid_(pager_koid),
      content_size_(vmo->size()) {
        vmo_->SetChildObserver(this);
    }

VmObjectDispatcher::~VmObjectDispatcher() {
    // Intentionally leave vmo_->user_id() set to our koid even though we're
    // dying and the koid will no longer map to a Dispatcher. koids are never
    // recycled, and it could be a useful breadcrumb.
    vmo_->SetChildObserver(nullptr);
}


void VmObjectDispatcher::OnZeroChild() {
    UpdateState(0, ZX_VMO_ZERO_CHILDREN);
}

void VmObjectDispatcher::OnOneChild() {
    UpdateState(ZX_VMO_ZERO_CHILDREN, 0);
}

void VmObjectDispatcher::on_zero_handles() {
    vmo_->CloseSource();
}

void VmObjectDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
    canary_.Assert();
    vmo_->get_name(out_name, ZX_MAX_NAME_LEN);
}

zx_status_t VmObjectDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();
    return vmo_->set_name(name, len);
}

zx_status_t VmObjectDispatcher::Read(user_out_ptr<void> user_data,
                                     size_t length,
                                     uint64_t offset) {
    canary_.Assert();

    return vmo_->ReadUser(user_data, offset, length);
}

zx_status_t VmObjectDispatcher::Write(user_in_ptr<const void> user_data,
                                      size_t length,
                                      uint64_t offset) {
    canary_.Assert();

    return vmo_->WriteUser(user_data, offset, length);
}

zx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    canary_.Assert();

    // Keep VMO content size synchronized with stream-visible size changes.
    zx_status_t status = vmo_->Resize(size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AutoLock lock(get_lock());
    content_size_ = size;
    return ZX_OK;
}

zx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    canary_.Assert();

    *size = vmo_->size();

    return ZX_OK;
}

zx_status_t VmObjectDispatcher::SetContentSize(uint64_t content_size) {
    canary_.Assert();

    fbl::AutoLock lock(get_lock());
    content_size_ = content_size;
    return ZX_OK;
}

uint64_t VmObjectDispatcher::GetContentSize() const {
    canary_.Assert();

    fbl::AutoLock lock(get_lock());
    return content_size_;
}

uint64_t VmObjectDispatcher::ExpandContentIfNeeded(uint64_t requested_content_size,
                                                   uint64_t zero_until_offset) {
    canary_.Assert();

    fbl::AutoLock lock(get_lock());
    if (requested_content_size <= content_size_) {
        return content_size_;
    }

    uint64_t previous_content_size = content_size_;
    uint64_t current_vmo_size = vmo_->size();
    if (requested_content_size > current_vmo_size) {
        // Guard against ROUNDUP wrapping past UINT64_MAX, which would
        // truncate the VMO instead of growing it.
        if (requested_content_size > UINT64_MAX - (PAGE_SIZE - 1)) {
            return content_size_;
        }
        zx_status_t status = vmo_->Resize(ROUNDUP(requested_content_size,
                                                  static_cast<uint64_t>(PAGE_SIZE)));
        if (status != ZX_OK) {
            return content_size_;
        }
    }

    content_size_ = requested_content_size;
    zero_until_offset = MIN(content_size_, zero_until_offset);
    if (zero_until_offset > previous_content_size) {
        static const uint8_t zeroes[PAGE_SIZE] = {};
        uint64_t offset = previous_content_size;
        while (offset < zero_until_offset) {
            size_t length = MIN(static_cast<uint64_t>(sizeof(zeroes)), zero_until_offset - offset);
            zx_status_t status = vmo_->Write(zeroes, offset, length);
            if (status != ZX_OK) {
                break;
            }
            offset += length;
        }
    }
    return content_size_;
}

zx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_inout_ptr<void> buffer, size_t buffer_size) {
    canary_.Assert();

    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu\n",
            op, offset, size, buffer.get(), buffer_size);

    switch (op) {
        case ZX_VMO_OP_COMMIT: {
            // TODO: handle partial commits
            auto status = vmo_->CommitRange(offset, size, nullptr);
            return status;
        }
        // Support pager hint opcodes needed by pager tests.
        case ZX_VMO_OP_ALWAYS_NEED: {
            if (offset + size < offset || offset + size > vmo_->size()) {
                return ZX_ERR_OUT_OF_RANGE;
            }
            if (size == 0) {
                return ZX_OK;
            }
            const uint64_t end = ROUNDUP(offset + size, static_cast<uint64_t>(PAGE_SIZE));
            for (uint64_t off = ROUNDDOWN(offset, static_cast<uint64_t>(PAGE_SIZE)); off < end;
                 off += PAGE_SIZE) {
                zx_status_t status = vmo_->CommitRange(off, PAGE_SIZE, nullptr);
                if (status == ZX_ERR_OUT_OF_RANGE) {
                    // The VMO shrank out from under the hint (it was in bounds when
                    // checked above). ALWAYS_NEED is best-effort, so terminate at the
                    // new boundary instead of failing, matching upstream.
                    break;
                }
                if (status != ZX_OK) {
                    return status;
                }
            }
            return ZX_OK;
        }
        case ZX_VMO_OP_DONT_NEED: {
            return (offset + size < offset || offset + size > vmo_->size()) ?
                ZX_ERR_OUT_OF_RANGE : ZX_OK;
        }
        case ZX_VMO_OP_DECOMMIT: {
            // TODO: handle partial decommits
            auto status = vmo_->DecommitRange(offset, size, nullptr);
            return status;
        }
        case ZX_VMO_OP_LOCK:
        case ZX_VMO_OP_UNLOCK:
            // TODO: handle
            return ZX_ERR_NOT_SUPPORTED;
        case ZX_VMO_OP_CACHE_SYNC:
            return vmo_->SyncCache(offset, size);
        case ZX_VMO_OP_CACHE_INVALIDATE:
            return vmo_->InvalidateCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN:
            return vmo_->CleanCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN_INVALIDATE:
            return vmo_->CleanInvalidateCache(offset, size);
        default:
            return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t VmObjectDispatcher::SetMappingCachePolicy(uint32_t cache_policy) {
    return vmo_->SetMappingCachePolicy(cache_policy);
}

zx_status_t VmObjectDispatcher::Clone(uint32_t options, uint64_t offset, uint64_t size,
        bool copy_name, fbl::RefPtr<VmObject>* clone_vmo) {
    canary_.Assert();

    LTRACEF("options 0x%x offset %#" PRIx64 " size %#" PRIx64 "\n",
            options, offset, size);

    if (options & ZX_VMO_CLONE_COPY_ON_WRITE) {
        return vmo_->CloneCOW(offset, size, copy_name, clone_vmo);
    } else {
        return ZX_ERR_INVALID_ARGS;
    }
}
