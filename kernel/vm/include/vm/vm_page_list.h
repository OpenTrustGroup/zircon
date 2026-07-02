// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <vm/vm.h>
#include <zircon/types.h>

struct vm_page;
class VmPageList;

class VmPageListNode final : public fbl::WAVLTreeContainable<fbl::unique_ptr<VmPageListNode>> {
public:
    explicit VmPageListNode(uint64_t offset);
    ~VmPageListNode();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageListNode);

    static const size_t kPageFanOut = 16;

    // accessors
    uint64_t offset() const { return obj_offset_; }
    uint64_t GetKey() const { return obj_offset_; }

    // for every valid page in the node call the passed in function
    template <typename T>
    zx_status_t ForEveryPage(T func, uint64_t start_offset, uint64_t end_offset) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        size_t start = 0;
        size_t end = kPageFanOut;
        if (start_offset > obj_offset_) {
            start = (start_offset - obj_offset_) / PAGE_SIZE;
        }
        if (end_offset < obj_offset_) {
            return ZX_ERR_NEXT;
        }
        if (end_offset < obj_offset_ + kPageFanOut * PAGE_SIZE) {
            end = (end_offset - obj_offset_) / PAGE_SIZE;
        }
        for (size_t i = start; i < end; i++) {
            if (pages_[i]) {
                zx_status_t status = func(pages_[i], obj_offset_ + i * PAGE_SIZE);
                if (unlikely(status != ZX_ERR_NEXT)) {
                    return status;
                }
            }
        }
        return ZX_ERR_NEXT;
    }

    // for every valid page in the node call the passed in function
    template <typename T>
    zx_status_t ForEveryPage(T func, uint64_t start_offset, uint64_t end_offset) const {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        size_t start = 0;
        size_t end = kPageFanOut;
        if (start_offset > obj_offset_) {
            start = (start_offset - obj_offset_) / PAGE_SIZE;
        }
        if (end_offset < obj_offset_) {
            return ZX_ERR_NEXT;
        }
        if (end_offset < obj_offset_ + kPageFanOut * PAGE_SIZE) {
            end = (end_offset - obj_offset_) / PAGE_SIZE;
        }
        for (size_t i = start; i < end; i++) {
            if (pages_[i]) {
                zx_status_t status = func(pages_[i], obj_offset_ + i * PAGE_SIZE);
                if (unlikely(status != ZX_ERR_NEXT)) {
                    return status;
                }
            }
        }
        return ZX_ERR_NEXT;
    }

    vm_page* GetPage(size_t index);
    vm_page* RemovePage(size_t index);
    zx_status_t AddPage(vm_page* p, size_t index);

    bool IsEmpty() const {
        for (const auto p : pages_) {
            if (p)
                return false;
        }
        return true;
    }

private:
    fbl::Canary<fbl::magic("PLST")> canary_;

    uint64_t obj_offset_ = 0;
    vm_page* pages_[kPageFanOut] = {};
};

// The pre-marker upstream VmPageSpliceList (Fuchsia commit ef9c451ba83) ported to
// the 2018 tree: it holds raw vm_page_t* in value-type head_/tail_ nodes plus a
// middle tree, and Pop() returns a vm_page*. F7 later reworked it on top of
// VmPageOrMarker page slots (Pop() returns a VmPageOrMarker); the 2018 page list
// has no page markers. See the note on VmPageList::TakePages in vm_page_list.cpp.
class VmPageSpliceList final {
public:
    VmPageSpliceList();
    VmPageSpliceList(VmPageSpliceList&& other);
    VmPageSpliceList& operator=(VmPageSpliceList&& other);
    ~VmPageSpliceList();

    // Pops the next page off of the splice.
    vm_page* Pop();

    // Returns true after the whole collection has been processed by Pop.
    bool IsDone() const { return pos_ >= length_; }

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmPageSpliceList);

private:
    VmPageSpliceList(uint64_t offset, uint64_t length);
    void FreeAllPages();

    uint64_t offset_;
    uint64_t length_;
    uint64_t pos_ = 0;

    // VmPageListNode is not movable, so head_/tail_ are value members (rather than
    // upstream's C++17 `= VmPageListNode(0)`, which relied on guaranteed copy
    // elision unavailable in the 2018 C++14 build); moves transfer node contents
    // via move_vm_page_list_node.
    VmPageListNode head_{0};
    fbl::WAVLTree<uint64_t, fbl::unique_ptr<VmPageListNode>> middle_;
    VmPageListNode tail_{0};

    friend class VmPageList;
};

class VmPageList final {
public:
    VmPageList();
    ~VmPageList();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageList);

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    zx_status_t ForEveryPage(T per_page_func) {
        for (auto& pl : list_) {
            zx_status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                                 pl.offset() + pl.kPageFanOut * PAGE_SIZE);
            if (unlikely(status != ZX_ERR_NEXT)) {
                if (status == ZX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return ZX_OK;
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    zx_status_t ForEveryPage(T per_page_func) const {
        for (auto& pl : list_) {
            zx_status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                                 pl.offset() + pl.kPageFanOut * PAGE_SIZE);
            if (unlikely(status != ZX_ERR_NEXT)) {
                if (status == ZX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return ZX_OK;
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    zx_status_t ForEveryPageInRange(T per_page_func, uint64_t start_offset, uint64_t end_offset) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        // Find the first node with a start after start_offset; if start_offset
        // is in a node, it'll be in the one before it.
        auto start = --list_.upper_bound(start_offset);
        if (!start.IsValid()) {
            start = list_.begin();
        }
        const auto end = list_.lower_bound(end_offset);
        for (auto itr = start; itr != end; ++itr) {
            auto& pl = *itr;
            zx_status_t status = pl.ForEveryPage(per_page_func, start_offset, end_offset);
            if (unlikely(status != ZX_ERR_NEXT)) {
                if (status == ZX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return ZX_OK;
    }

    template <typename T>
    zx_status_t ForEveryPageInRange(T per_page_func, uint64_t start_offset,
                                    uint64_t end_offset) const {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        // Find the first node with a start after start_offset; if start_offset
        // is in a node, it'll be in the one before it.
        auto start = --list_.upper_bound(start_offset);
        if (!start.IsValid()) {
            start = list_.begin();
        }
        const auto end = list_.lower_bound(end_offset);
        for (auto itr = start; itr != end; ++itr) {
            auto& pl = *itr;
            zx_status_t status = pl.ForEveryPage(per_page_func, start_offset, end_offset);
            if (unlikely(status != ZX_ERR_NEXT)) {
                if (status == ZX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return ZX_OK;
    }

    zx_status_t AddPage(vm_page*, uint64_t offset);
    vm_page* GetPage(uint64_t offset);
    // Removes the page at |offset| from the list, returning it via |page_out|.
    // Returns true if a page was present. Used by TakePages.
    bool RemovePage(uint64_t offset, vm_page** page_out);
    zx_status_t FreePage(uint64_t offset);
    size_t FreeAllPages();
    bool IsEmpty();

    // Pager support.
    VmPageSpliceList TakePages(uint64_t offset, uint64_t length);

private:
    fbl::WAVLTree<uint64_t, fbl::unique_ptr<VmPageListNode>> list_;
};
