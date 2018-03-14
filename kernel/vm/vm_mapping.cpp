// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_address_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <zircon/types.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmMapping::VmMapping(VmAddressRegion& parent, vaddr_t base, size_t size, uint32_t vmar_flags,
                     fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset, uint arch_mmu_flags)
    : VmAddressRegionOrMapping(base, size, vmar_flags,
                               parent.aspace_.get(), &parent),
      object_(fbl::move(vmo)), object_offset_(vmo_offset), arch_mmu_flags_(arch_mmu_flags) {

    LTRACEF("%p aspace %p base %#" PRIxPTR " size %#zx offset %#" PRIx64 "\n",
            this, aspace_.get(), base_, size_, vmo_offset);
}

VmMapping::~VmMapping() {
    canary_.Assert();
    LTRACEF("%p aspace %p base %#" PRIxPTR " size %#zx\n",
            this, aspace_.get(), base_, size_);
}

size_t VmMapping::AllocatedPagesLocked() const {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    if (state_ != LifeCycleState::ALIVE) {
        return 0;
    }
    return object_->AllocatedPagesInRange(object_offset_, size_);
}

void VmMapping::Dump(uint depth, bool verbose) const {
    canary_.Assert();
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    char vmo_name[32];
    object_->get_name(vmo_name, sizeof(vmo_name));
    printf("map %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx mmufl %#x\n",
           this, base_, base_ + size_ - 1, size_, arch_mmu_flags_);
    for (uint i = 0; i < depth + 1; ++i) {
        printf("  ");
    }
    printf("vmo %p/k%" PRIu64 " off %#" PRIx64
           " pages %zu ref %d '%s'\n",
           object_.get(), object_->user_id(), object_offset_,
           // TODO(dbort): Use AllocatePagesLocked() once Dump() is locked
           // consistently. Currently, Dump() may be called without the aspace
           // lock.
           object_->AllocatedPagesInRange(object_offset_, size_),
           ref_count_debug(), vmo_name);
    if (verbose)
        object_->Dump(depth + 1, false);
}

zx_status_t VmMapping::Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
    canary_.Assert();
    LTRACEF("%p %#" PRIxPTR " %#x %#x\n", this, base_, flags_, new_arch_mmu_flags);

    if (!IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    if (size == 0 || !is_in_range(base, size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ProtectLocked(base, size, new_arch_mmu_flags);
}

namespace {

// Implementation helper for ProtectLocked
zx_status_t ProtectOrUnmap(const fbl::RefPtr<VmAspace>& aspace, vaddr_t base, size_t size,
                           uint new_arch_mmu_flags) {
    if (new_arch_mmu_flags & ARCH_MMU_FLAG_PERM_RWX_MASK) {
        return aspace->arch_aspace().Protect(base, size / PAGE_SIZE, new_arch_mmu_flags);
    } else {
        return aspace->arch_aspace().Unmap(base, size / PAGE_SIZE, nullptr);
    }
}

} // namespace

zx_status_t VmMapping::ProtectLocked(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(size != 0 && IS_PAGE_ALIGNED(base) && IS_PAGE_ALIGNED(size));

    // Do not allow changing caching
    if (new_arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (!is_valid_mapping_flags(new_arch_mmu_flags)) {
        return ZX_ERR_ACCESS_DENIED;
    }

    DEBUG_ASSERT(object_);
    // grab the lock for the vmo
    AutoLock al(object_->lock());

    // Persist our current caching mode
    new_arch_mmu_flags |= (arch_mmu_flags_ & ARCH_MMU_FLAG_CACHE_MASK);

    // If we're not actually changing permissions, return fast.
    if (new_arch_mmu_flags == arch_mmu_flags_) {
        return ZX_OK;
    }

    // TODO(teisenbe): deal with error mapping on arch_mmu_protect fail

    // If we're changing the whole mapping, just make the change.
    if (base_ == base && size_ == size) {
        zx_status_t status = ProtectOrUnmap(aspace_, base, size, new_arch_mmu_flags);
        LTRACEF("arch_mmu_protect returns %d\n", status);
        arch_mmu_flags_ = new_arch_mmu_flags;
        return ZX_OK;
    }

    // Handle changing from the left
    if (base_ == base) {
        // Create a new mapping for the right half (has old perms)
        fbl::AllocChecker ac;
        fbl::RefPtr<VmMapping> mapping(fbl::AdoptRef(
            new (&ac) VmMapping(*parent_, base + size, size_ - size, flags_,
                                object_, object_offset_ + size, arch_mmu_flags_)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zx_status_t status = ProtectOrUnmap(aspace_, base, size, new_arch_mmu_flags);
        LTRACEF("arch_mmu_protect returns %d\n", status);
        arch_mmu_flags_ = new_arch_mmu_flags;

        size_ = size;
        mapping->ActivateLocked();
        return ZX_OK;
    }

    // Handle changing from the right
    if (base_ + size_ == base + size) {
        // Create a new mapping for the right half (has new perms)
        fbl::AllocChecker ac;

        fbl::RefPtr<VmMapping> mapping(fbl::AdoptRef(
            new (&ac) VmMapping(*parent_, base, size, flags_,
                                object_, object_offset_ + base - base_,
                                new_arch_mmu_flags)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zx_status_t status = ProtectOrUnmap(aspace_, base, size, new_arch_mmu_flags);
        LTRACEF("arch_mmu_protect returns %d\n", status);

        size_ -= size;
        mapping->ActivateLocked();
        return ZX_OK;
    }

    // We're unmapping from the center, so we need to create two new mappings
    const size_t left_size = base - base_;
    const size_t right_size = (base_ + size_) - (base + size);
    const uint64_t center_vmo_offset = object_offset_ + base - base_;
    const uint64_t right_vmo_offset = center_vmo_offset + size;

    fbl::AllocChecker ac;
    fbl::RefPtr<VmMapping> center_mapping(fbl::AdoptRef(
        new (&ac) VmMapping(*parent_, base, size, flags_,
                            object_, center_vmo_offset, new_arch_mmu_flags)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    fbl::RefPtr<VmMapping> right_mapping(fbl::AdoptRef(
        new (&ac) VmMapping(*parent_, base + size, right_size, flags_,
                            object_, right_vmo_offset, arch_mmu_flags_)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = ProtectOrUnmap(aspace_, base, size, new_arch_mmu_flags);
    LTRACEF("arch_mmu_protect returns %d\n", status);

    // Turn us into the left half
    size_ = left_size;

    center_mapping->ActivateLocked();
    right_mapping->ActivateLocked();
    return ZX_OK;
}

zx_status_t VmMapping::Unmap(vaddr_t base, size_t size) {
    LTRACEF("%p %#" PRIxPTR " %zu\n", this, base, size);

    if (!IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    size = ROUNDUP(size, PAGE_SIZE);

    fbl::RefPtr<VmAspace> aspace(aspace_);
    if (!aspace) {
        return ZX_ERR_BAD_STATE;
    }

    AutoLock guard(aspace->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    if (size == 0 || !is_in_range(base, size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // If we're unmapping everything, destroy this mapping
    if (base == base_ && size == size_) {
        return DestroyLocked();
    }

    return UnmapLocked(base, size);
}

zx_status_t VmMapping::UnmapLocked(vaddr_t base, size_t size) {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(size != 0 && IS_PAGE_ALIGNED(size) && IS_PAGE_ALIGNED(base));
    DEBUG_ASSERT(base >= base_ && base - base_ < size_);
    DEBUG_ASSERT(size_ - (base - base_) >= size);
    DEBUG_ASSERT(parent_);

    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    // If our parent VMAR is DEAD, then we can only unmap everything.
    DEBUG_ASSERT(parent_->state_ != LifeCycleState::DEAD || (base == base_ && size == size_));

    LTRACEF("%p\n", this);

    // grab the lock for the vmo
    DEBUG_ASSERT(object_);
    AutoLock al(object_->lock());

    // Check if unmapping from one of the ends
    if (base_ == base || base + size == base_ + size_) {
        LTRACEF("unmapping base %#lx size %#zx\n", base, size);
        zx_status_t status = aspace_->arch_aspace().Unmap(base, size / PAGE_SIZE, nullptr);
        if (status < 0) {
            return status;
        }

        if (base_ == base && size_ != size) {
            // We need to remove ourselves from tree before updating base_,
            // since base_ is the tree key.
            fbl::RefPtr<VmAddressRegionOrMapping> ref(parent_->subregions_.erase(*this));
            base_ += size;
            object_offset_ += size;
            parent_->subregions_.insert(fbl::move(ref));
        }
        size_ -= size;

        return ZX_OK;
    }

    // We're unmapping from the center, so we need to split the mapping
    DEBUG_ASSERT(parent_->state_ == LifeCycleState::ALIVE);

    const uint64_t vmo_offset = object_offset_ + (base + size) - base_;
    const vaddr_t new_base = base + size;
    const size_t new_size = (base_ + size_) - new_base;

    fbl::AllocChecker ac;
    fbl::RefPtr<VmMapping> mapping(fbl::AdoptRef(
        new (&ac) VmMapping(*parent_, new_base, new_size, flags_, object_, vmo_offset,
                            arch_mmu_flags_)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Unmap the middle segment
    LTRACEF("unmapping base %#lx size %#zx\n", base, size);
    zx_status_t status = aspace_->arch_aspace().Unmap(base, size / PAGE_SIZE, nullptr);
    if (status < 0) {
        return status;
    }

    // Turn us into the left half
    size_ = base - base_;
    mapping->ActivateLocked();
    return ZX_OK;
}

zx_status_t VmMapping::UnmapVmoRangeLocked(uint64_t offset, uint64_t len) const {
    canary_.Assert();

    LTRACEF("region %p obj_offset %#" PRIx64 " size %zu, offset %#" PRIx64 " len %#" PRIx64 "\n",
            this, object_offset_, size_, offset, len);

    // NOTE: must be acquired with the vmo lock held, but doesn't need to take
    // the address space lock, since it will not manipulate its location in the
    // vmar tree. However, it must be held in the ALIVE state across this call.
    //
    // Avoids a race with DestroyLocked() since it removes ourself from the VMO's
    // mapping list with the VMO lock held before dropping this state to DEAD. The
    // VMO cant call back to us once we're out of their list.
    DEBUG_ASSERT(state_ == LifeCycleState::ALIVE);

    DEBUG_ASSERT(object_);
    DEBUG_ASSERT(object_->lock()->IsHeld());

    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
    DEBUG_ASSERT(len > 0);

    // If we're currently faulting and are responsible for the vmo code to be calling
    // back to us, detect the recursion and abort here.
    // The specific path we're avoiding is if the VMO calls back into us during vmo->GetPageLocked()
    // via UnmapVmoRangeLocked(). If we set this flag we're short circuiting the unmap operation
    // so that we don't do extra work.
    if (likely(currently_faulting_)) {
        LTRACEF("recursing to ourself, abort\n");
        return ZX_OK;
    }

    if (len == 0)
        return ZX_OK;

    // compute the intersection of the passed in vmo range and our mapping
    uint64_t offset_new;
    uint64_t len_new;
    if (!GetIntersect(object_offset_, static_cast<uint64_t>(size_), offset, len,
                      &offset_new, &len_new))
        return ZX_OK;

    DEBUG_ASSERT(len_new > 0 && len_new <= SIZE_MAX);
    DEBUG_ASSERT(offset_new >= object_offset_);

    LTRACEF("intersection offset %#" PRIx64 ", len %#" PRIx64 "\n", offset_new, len_new);

    // make sure the base + offset is within our address space
    // should be, according to the range stored in base_ + size_
    vaddr_t unmap_base;
    bool overflowed = add_overflow(base_, offset_new - object_offset_, &unmap_base);
    ASSERT(!overflowed);

    // make sure we're only unmapping within our window
    ASSERT(unmap_base >= base_);
    ASSERT((unmap_base + len_new - 1) <= (base_ + size_ - 1));

    LTRACEF("going to unmap %#" PRIxPTR ", len %#" PRIx64 " aspace %p\n",
            unmap_base, len_new, aspace_.get());

    zx_status_t status = aspace_->arch_aspace().Unmap(unmap_base,
                                                      static_cast<size_t>(len_new) / PAGE_SIZE, nullptr);
    if (status < 0)
        return status;

    return ZX_OK;
}

namespace {

class VmMappingCoalescer {
public:
    VmMappingCoalescer(VmMapping* mapping, vaddr_t base);
    ~VmMappingCoalescer();

    // Add a page to the mapping run.  If this fails, the VmMappingCoalescer is
    // no longer valid.
    zx_status_t Append(vaddr_t vaddr, paddr_t paddr) {
        DEBUG_ASSERT(!aborted_);
        // If this isn't the expected vaddr, flush the run we have first.
        if (count_ >= fbl::count_of(phys_) || vaddr != base_ + count_ * PAGE_SIZE) {
            zx_status_t status = Flush();
            if (status != ZX_OK) {
                return status;
            }
            base_ = vaddr;
        }
        phys_[count_] = paddr;
        ++count_;
        return ZX_OK;
    }

    // Submit any outstanding mappings to the MMU.  If this fails, the
    // VmMappingCoalescer is no longer valid.
    zx_status_t Flush();

    // Drop the current outstanding mappings without sending them to the MMU.
    // After this call, the VmMappingCoalescer is no longer valid.
    void Abort() {
        aborted_ = true;
    }
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmMappingCoalescer);

    VmMapping* mapping_;
    vaddr_t base_;
    paddr_t phys_[16];
    size_t count_;
    bool aborted_;
};

VmMappingCoalescer::VmMappingCoalescer(VmMapping* mapping, vaddr_t base)
    : mapping_(mapping), base_(base), count_(0), aborted_(false) { }

VmMappingCoalescer::~VmMappingCoalescer() {
    // Make sure we've flushed or aborted
    DEBUG_ASSERT(count_ == 0 || aborted_);
}

zx_status_t VmMappingCoalescer::Flush() {
    if (count_ == 0) {
        return ZX_OK;
    }

    uint flags = mapping_->arch_mmu_flags();
    if (flags & ARCH_MMU_FLAG_PERM_RWX_MASK) {
        size_t mapped;
        zx_status_t ret = mapping_->aspace()->arch_aspace().Map(base_, phys_, count_, flags,
                                                                &mapped);
        if (ret != ZX_OK) {
            TRACEF("error %d mapping %zu pages starting at va %#" PRIxPTR "\n", ret, count_, base_);
            aborted_ = true;
            return ret;
        }
        DEBUG_ASSERT(mapped == count_);
    }
    base_ += count_ * PAGE_SIZE;
    count_ = 0;
    return ZX_OK;
}

} // namespace

zx_status_t VmMapping::MapRange(size_t offset, size_t len, bool commit) {
    canary_.Assert();

    len = ROUNDUP(len, PAGE_SIZE);
    if (len == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    LTRACEF("region %p, offset %#zx, size %#zx, commit %d\n", this, offset, len, commit);

    DEBUG_ASSERT(object_);
    if (!IS_PAGE_ALIGNED(offset) || !is_in_range(base_ + offset, len)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // precompute the flags we'll pass GetPageLocked
    // if committing, then tell it to soft fault in a page
    uint pf_flags = VMM_PF_FLAG_WRITE;
    if (commit)
        pf_flags |= VMM_PF_FLAG_SW_FAULT;

    // grab the lock for the vmo
    AutoLock al(object_->lock());

    // set the currently faulting flag for any recursive calls the vmo may make back into us.
    DEBUG_ASSERT(!currently_faulting_);
    currently_faulting_ = true;
    auto ac = fbl::MakeAutoCall([&]() { currently_faulting_ = false; });

    // iterate through the range, grabbing a page from the underlying object and
    // mapping it in
    size_t o;
    VmMappingCoalescer coalescer(this, base_ + offset);
    for (o = offset; o < offset + len; o += PAGE_SIZE) {
        uint64_t vmo_offset = object_offset_ + o;

        zx_status_t status;
        paddr_t pa;
        status = object_->GetPageLocked(vmo_offset, pf_flags, nullptr, nullptr, &pa);
        if (status < 0) {
            // no page to map
            if (commit) {
                // fail when we can't commit every requested page
                coalescer.Abort();
                return status;
            }

            // skip ahead
            continue;
        }

        vaddr_t va = base_ + o;
        LTRACEF_LEVEL(2, "mapping pa %#" PRIxPTR " to va %#" PRIxPTR "\n", pa, va);
        status = coalescer.Append(va, pa);
        if (status != ZX_OK) {
            return status;
        }
    }
    return coalescer.Flush();
}

zx_status_t VmMapping::DecommitRange(size_t offset, size_t len,
                                     size_t* decommitted) {
    canary_.Assert();
    LTRACEF("%p [%#zx+%#zx], offset %#zx, len %#zx\n",
            this, base_, size_, offset, len);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }
    if (offset + len < offset || offset + len > size_) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    // VmObject::DecommitRange will typically call back into our instance's
    // VmMapping::UnmapVmoRangeLocked.
    return object_->DecommitRange(object_offset_ + offset, len, decommitted);
}

zx_status_t VmMapping::DestroyLocked() {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    LTRACEF("%p\n", this);

    // Take a reference to ourself, so that we do not get destructed after
    // dropping our last reference in this method (e.g. when calling
    // subregions_.erase below).
    fbl::RefPtr<VmMapping> self(this);

#if WITH_LIB_VDSO
    // The vDSO code mapping can never be unmapped, not even
    // by VMAR destruction (except for process exit, of course).
    // TODO(mcgrathr): Turn this into a policy-driven process-fatal case
    // at some point.  teisenbe@ wants to eventually make zx_vmar_destroy
    // never fail.
    if (aspace_->vdso_code_mapping_ == self)
        return ZX_ERR_ACCESS_DENIED;
#endif

    // unmap our entire range
    zx_status_t status = UnmapLocked(base_, size_);
    if (status != ZX_OK) {
        return status;
    }

    // Unmap should have reset our size to 0
    DEBUG_ASSERT(size_ == 0);

    // grab the object lock and remove ourself from its list
    {
        AutoLock al(object_->lock());
        object_->RemoveMappingLocked(this);
    }

    // detach from any object we have mapped
    object_.reset();

    // Detach the now dead region from the parent
    if (parent_) {
        DEBUG_ASSERT(subregion_list_node_.InContainer());
        parent_->RemoveSubregion(this);
    }

    // mark ourself as dead
    parent_ = nullptr;
    state_ = LifeCycleState::DEAD;
    return ZX_OK;
}

zx_status_t VmMapping::PageFault(vaddr_t va, const uint pf_flags) {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    DEBUG_ASSERT(va >= base_ && va <= base_ + size_ - 1);

    va = ROUNDDOWN(va, PAGE_SIZE);
    uint64_t vmo_offset = va - base_ + object_offset_;

    __UNUSED char pf_string[5];
    LTRACEF("%p va %#" PRIxPTR " vmo_offset %#" PRIx64 ", pf_flags %#x (%s)\n",
            this, va, vmo_offset, pf_flags,
            vmm_pf_flags_to_string(pf_flags, pf_string));

    // make sure we have permission to continue
    if ((pf_flags & VMM_PF_FLAG_USER) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_USER)) {
        // user page fault on non user mapped region
        LTRACEF("permission failure: user fault on non user region\n");
        return ZX_ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_WRITE) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_WRITE)) {
        // write to a non-writeable region
        LTRACEF("permission failure: write fault on non-writable region\n");
        return ZX_ERR_ACCESS_DENIED;
    }
    if (!(pf_flags & VMM_PF_FLAG_WRITE) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_READ)) {
        // read to a non-readable region
        LTRACEF("permission failure: read fault on non-readable region\n");
        return ZX_ERR_ACCESS_DENIED;
    }
    if ((pf_flags & VMM_PF_FLAG_INSTRUCTION) && !(arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_EXECUTE)) {
        // instruction fetch from a no execute region
        LTRACEF("permission failure: execute fault on no execute region\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    // grab the lock for the vmo
    AutoLock al(object_->lock());

    // set the currently faulting flag for any recursive calls the vmo may make back into us
    // The specific path we're avoiding is if the VMO calls back into us during vmo->GetPageLocked()
    // via UnmapVmoRangeLocked(). Since we're responsible for that page, signal to ourself to skip
    // the unmap operation.
    DEBUG_ASSERT(!currently_faulting_);
    currently_faulting_ = true;
    auto ac = fbl::MakeAutoCall([&]() { currently_faulting_ = false; });

    // fault in or grab an existing page
    paddr_t new_pa;
    vm_page_t* page;
    zx_status_t status = object_->GetPageLocked(vmo_offset, pf_flags, nullptr, &page, &new_pa);
    if (status < 0) {
        TRACEF("ERROR: failed to fault in or grab existing page\n");
        TRACEF("%p vmo_offset %#" PRIx64 ", pf_flags %#x\n", this, vmo_offset, pf_flags);
        return status;
    }

    // if we read faulted, make sure we map or modify the page without any write permissions
    // this ensures we will fault again if a write is attempted so we can potentially
    // replace this page with a copy or a new one
    uint mmu_flags = arch_mmu_flags_;
    if (!(pf_flags & VMM_PF_FLAG_WRITE)) {
        // we read faulted, so only map with read permissions
        mmu_flags &= ~ARCH_MMU_FLAG_PERM_WRITE;
    }

    // see if something is mapped here now
    // this may happen if we are one of multiple threads racing on a single address
    uint page_flags;
    paddr_t pa;
    zx_status_t err = aspace_->arch_aspace().Query(va, &pa, &page_flags);
    if (err >= 0) {
        LTRACEF("queried va, page at pa %#" PRIxPTR ", flags %#x is already there\n", pa,
                page_flags);
        if (pa == new_pa) {
            // page was already mapped, are the permissions compatible?
            // test that the page is already mapped with either the region's mmu flags
            // or the flags that we're about to try to switch it to, which may be read-only
            if (page_flags == arch_mmu_flags_ || page_flags == mmu_flags)
                return ZX_OK;

            // assert that we're not accidentally marking the zero page writable
            DEBUG_ASSERT((pa != vm_get_zero_page_paddr()) || !(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE));

            // same page, different permission
            status = aspace_->arch_aspace().Protect(va, 1, mmu_flags);
            if (status < 0) {
                TRACEF("failed to modify permissions on existing mapping\n");
                return ZX_ERR_NO_MEMORY;
            }
        } else {
            // some other page is mapped there already
            LTRACEF("thread %s faulted on va %#" PRIxPTR ", different page was present\n",
                    get_current_thread()->name, va);
            LTRACEF("old pa %#" PRIxPTR " new pa %#" PRIxPTR "\n", pa, new_pa);

            // assert that we're not accidentally mapping the zero page writable
            DEBUG_ASSERT((new_pa != vm_get_zero_page_paddr()) || !(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE));

            // unmap the old one and put the new one in place
            status = aspace_->arch_aspace().Unmap(va, 1, nullptr);
            if (status < 0) {
                TRACEF("failed to remove old mapping before replacing\n");
                return ZX_ERR_NO_MEMORY;
            }

            size_t mapped;
            status = aspace_->arch_aspace().MapContiguous(va, new_pa, 1, mmu_flags, &mapped);
            if (status < 0) {
                TRACEF("failed to map replacement page\n");
                return ZX_ERR_NO_MEMORY;
            }
            DEBUG_ASSERT(mapped == 1);

            return ZX_OK;
        }
    } else {
        // nothing was mapped there before, map it now
        LTRACEF("mapping pa %#" PRIxPTR " to va %#" PRIxPTR " is zero page %d\n",
                new_pa, va, (new_pa == vm_get_zero_page_paddr()));

        // assert that we're not accidentally mapping the zero page writable
        DEBUG_ASSERT((new_pa != vm_get_zero_page_paddr()) || !(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE));

        size_t mapped;
        status = aspace_->arch_aspace().MapContiguous(va, new_pa, 1, mmu_flags, &mapped);
        if (status < 0) {
            TRACEF("failed to map page\n");
            return ZX_ERR_NO_MEMORY;
        }
        DEBUG_ASSERT(mapped == 1);
    }

// TODO: figure out what to do with this
#if ARCH_ARM64
    if (pf_flags & VMM_PF_FLAG_GUEST) {
        // TODO(abdulla): Correctly handle page fault for guest.
    } else if (arch_mmu_flags_ & ARCH_MMU_FLAG_PERM_EXECUTE) {
        arch_sync_cache_range(va, PAGE_SIZE);
    }
#endif
    return ZX_OK;
}

// We disable thread safety analysis here because one of the common uses of this
// function is for splitting one mapping object into several that will be backed
// by the same VmObject.  In that case, object_->lock() gets aliased across all
// of the VmMappings involved, but we have no way of informing the analyzer of
// this, resulting in spurious warnings.  We could disable analysis on the
// splitting functions instead, but they are much more involved, and we'd rather
// have the analysis mostly functioning on those than on this much simpler
// function.
void VmMapping::ActivateLocked() TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(object_->lock()->IsHeld());
    DEBUG_ASSERT(parent_);

    state_ = LifeCycleState::ALIVE;
    object_->AddMappingLocked(this);
    parent_->subregions_.insert(fbl::RefPtr<VmAddressRegionOrMapping>(this));
}

void VmMapping::Activate() {
    AutoLock guard(object_->lock());
    ActivateLocked();
}
