// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_OR_MARKER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_OR_MARKER_H_

#include <vm/page.h>

// A minimal stand-in for upstream's VmPageOrMarker, provided so that upstream
// paged-VMO code (e.g. VmCowPages::SupplyPagesLocked) can be ported into the 2018
// VmObjectPaged with as little change as possible.
//
// Upstream stores each page-list slot as a VmPageOrMarker, a tagged value that is
// either empty, a real page, or a "marker" (a zero-page sentinel that resolves a
// pager fault without allocating). The 2018 page list stores raw vm_page_t* and
// has no markers, so this type only tracks the empty/page/marker tag; the actual
// no-marker adaptation (materializing a marker as an allocated zero page) lives in
// VmObjectPaged::AddPageLocked(VmPageOrMarker*, ...).
class VmPageOrMarker {
public:
    VmPageOrMarker() = default;
    // Implicit conversion from the raw vm_page_t* that the 2018 VmPageSpliceList
    // yields: a null page is an empty slot, a real page is a page.
    VmPageOrMarker(vm_page_t* page) : type_(page ? Type::Page : Type::Empty), page_(page) {}

    static VmPageOrMarker Marker() { return VmPageOrMarker(Type::Marker, nullptr); }
    static VmPageOrMarker Page(vm_page_t* page) { return VmPageOrMarker(Type::Page, page); }

    bool IsEmpty() const { return type_ == Type::Empty; }
    bool IsPage() const { return type_ == Type::Page; }
    bool IsMarker() const { return type_ == Type::Marker; }

    vm_page_t* Page() const { return page_; }

    // Relinquish ownership of the page, leaving the slot empty.
    vm_page_t* ReleasePage() {
        vm_page_t* page = page_;
        page_ = nullptr;
        type_ = Type::Empty;
        return page;
    }

private:
    enum class Type { Empty, Page, Marker };
    VmPageOrMarker(Type type, vm_page_t* page) : type_(type), page_(page) {}

    Type type_ = Type::Empty;
    vm_page_t* page_ = nullptr;
};

// Upstream tags for AddPageLocked overwrite policy. Only the value used by the
// ported supply path (None) is meaningful in 2018; the 2018 page list never
// overwrites, so the tag is accepted and ignored.
enum class CanOverwriteContent { None, Zero, NonZero };

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_OR_MARKER_H_
