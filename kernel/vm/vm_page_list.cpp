// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_page_list.h>

#include <err.h>
#include <fbl/alloc_checker.h>
#include <inttypes.h>
#include <ktl/move.h>
#include <trace.h>
#include <vm/page_queues.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <zircon/types.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

namespace {

inline uint64_t offset_to_node_offset(uint64_t offset) {
    return ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
}

inline uint64_t offset_to_node_index(uint64_t offset) {
    return (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;
}

// Move the pages out of the (assumed empty) |dest| node's slots into |src|'s.
// VmPageListNode is not movable (it lives in an intrusive tree), so the splice
// list transfers node contents rather than moving the nodes themselves.
inline void move_vm_page_list_node(VmPageListNode* dest, VmPageListNode* src) {
    ASSERT(dest->IsEmpty());

    for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
        vm_page* page = src->RemovePage(i);
        if (page) {
            dest->AddPage(page, i);
        }
    }
}

}  // namespace

VmPageListNode::VmPageListNode(uint64_t offset)
    : obj_offset_(offset) {
    LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
}

VmPageListNode::~VmPageListNode() {
    LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
    canary_.Assert();

    for (__UNUSED auto p : pages_) {
        DEBUG_ASSERT(p == nullptr);
    }
}

vm_page* VmPageListNode::GetPage(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    return pages_[index];
}

vm_page* VmPageListNode::RemovePage(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);

    auto p = pages_[index];
    if (!p)
        return nullptr;

    pages_[index] = nullptr;

    return p;
}

zx_status_t VmPageListNode::AddPage(vm_page* p, size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    if (pages_[index])
        return ZX_ERR_ALREADY_EXISTS;
    pages_[index] = p;
    return ZX_OK;
}

VmPageList::VmPageList() {
    LTRACEF("%p\n", this);
}

VmPageList::~VmPageList() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(list_.is_empty());
}

zx_status_t VmPageList::AddPage(vm_page* p, uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p page %p, offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, p, offset,
                  node_offset, index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<VmPageListNode> pl =
            fbl::unique_ptr<VmPageListNode>(new (&ac) VmPageListNode(node_offset));
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;

        LTRACEF("allocating new inner node %p\n", pl.get());
        __UNUSED auto status = pl->AddPage(p, index);
        DEBUG_ASSERT(status == ZX_OK);

        list_.insert(ktl::move(pl));
    } else {
        pln->AddPage(p, index);
    }

    return ZX_OK;
}

vm_page* VmPageList::GetPage(uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return nullptr;
    }

    return pln->GetPage(index);
}

zx_status_t VmPageList::FreePage(uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return ZX_ERR_NOT_FOUND;
    }

    // free this page
    auto page = pln->RemovePage(index);
    if (page) {
        // if it was the last page in the node, remove the node from the tree
        if (pln->IsEmpty()) {
            LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
            list_.erase(*pln);
        }

        // The page is leaving this VMO's live page list; take it out of its page
        // queue before its queue_node is reused by the pmm free path. Use the
        // tolerant variant because non-object pages (e.g. wired pages inserted by
        // CreateFromROData) are never queued.
        pmm_page_queues().RemoveIfQueued(page);

        pmm_free_page(page);
    }

    return ZX_OK;
}

size_t VmPageList::FreeAllPages() {
    LTRACEF("%p\n", this);

    list_node list;
    list_initialize(&list);

    size_t count = 0;

    // per page get a reference to the page pointer inside the page list node
    auto per_page_func = [&](vm_page*& p, uint64_t offset) {

        // The page is leaving this VMO's live page list. It must be taken out of its
        // page queue before its queue_node is reused below to build the pmm free
        // list, otherwise the queue would be corrupted. Use the tolerant variant as
        // non-object pages (e.g. wired pages from CreateFromROData) are never queued.
        pmm_page_queues().RemoveIfQueued(p);

        // add the page to our list and null out the inner node
        list_add_tail(&list, &p->queue_node);
        p = nullptr;
        count++;
        return ZX_ERR_NEXT;
    };

    // walk the tree in order, freeing all the pages on every node
    ForEveryPage(per_page_func);

    // return all the pages to the pmm at once
    __UNUSED auto freed = pmm_free(&list);
    DEBUG_ASSERT(freed == count);

    // empty the tree
    list_.clear();

    return count;
}

bool VmPageList::IsEmpty() {
    return list_.is_empty();
}

// [PORT-BLOCKED] The live VmPageList::TakePages below is the upstream
// implementation from before page markers were introduced (Fuchsia commit
// ef9c451ba83), ported to the 2018 tree (ktl:: -> fbl::). F7 later rebuilt it on
// VmPageOrMarker page slots with value-type head_/tail_ nodes accessed via
// Lookup() and list_skew_ for child lists, none of which exist in the 2018 page
// list, so that newer F7 version is preserved here only for reference.
/*
VmPageSpliceList VmPageList::TakePages(uint64_t offset, uint64_t length) {
  VmPageSpliceList res(offset, length);
  const uint64_t end = offset + length;

  // Taking pages from children isn't supported, so list_skew_ should be 0.
  DEBUG_ASSERT(list_skew_ == 0);

  // If we can't take the whole node at the start of the range,
  // the shove the pages into the splice list head_ node.
  while (offset_to_node_index(offset, 0) != 0 && offset < end) {
    res.head_.Lookup(offset_to_node_index(offset, 0)) = RemovePage(offset);
    offset += PAGE_SIZE;
  }

  // As long as the current and end node offsets are different, we
  // can just move the whole node into the splice list.
  while (offset_to_node_offset(offset, 0) != offset_to_node_offset(end, 0)) {
    ktl::unique_ptr<VmPageListNode> node = list_.erase(offset_to_node_offset(offset, 0));
    if (node) {
      res.middle_.insert(ktl::move(node));
    }
    offset += (PAGE_SIZE * VmPageListNode::kPageFanOut);
  }

  // Move any remaining pages into the splice list tail_ node.
  while (offset < end) {
    res.tail_.Lookup(offset_to_node_index(offset, 0)) = RemovePage(offset);
    offset += PAGE_SIZE;
  }

  return res;
}
*/
bool VmPageList::RemovePage(uint64_t offset, vm_page** page_out) {
    DEBUG_ASSERT(page_out);

    uint64_t node_offset = offset_to_node_offset(offset);
    size_t index = offset_to_node_index(offset);

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                  node_offset, index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return false;
    }

    // free this page
    auto page = pln->RemovePage(index);
    if (page) {
        // if it was the last page in the node, remove the node from the tree
        if (pln->IsEmpty()) {
            LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
            list_.erase(*pln);
        }

        // The page is being removed from this VMO's live page list (directly, or on
        // behalf of TakePages, which then owns it in a splice list). Take it out of
        // its page queue here so the caller receives a page that is no longer queued
        // and whose queue_node is free to reuse. Use the tolerant variant as
        // non-object pages are never queued.
        pmm_page_queues().RemoveIfQueued(page);

        *page_out = page;
        return true;
    } else {
        return false;
    }
}

VmPageSpliceList VmPageList::TakePages(uint64_t offset, uint64_t length) {
    VmPageSpliceList res(offset, length);
    const uint64_t end = offset + length;

    // If we can't take the whole node at the start of the range,
    // the shove the pages into the splice list head_ node.
    while (offset_to_node_index(offset) != 0 && offset < end) {
        vm_page_t* page;
        if (RemovePage(offset, &page)) {
            res.head_.AddPage(page, offset_to_node_index(offset));
        }
        offset += PAGE_SIZE;
    }

    // As long as the current and end node offsets are different, we
    // can just move the whole node into the splice list.
    while (offset_to_node_offset(offset) != offset_to_node_offset(end)) {
        fbl::unique_ptr<VmPageListNode> node = list_.erase(offset_to_node_offset(offset));
        if (node) {
            // The whole node is leaving the live page list into the splice list, which is
            // not part of any VMO's page queue. A bulk node move bypasses the per-offset
            // RemovePage path, so dequeue each page here to keep the page queues consistent;
            // otherwise a taken page stays linked in the source queue and a later SupplyPages
            // that re-adds it corrupts the queue lists.
            node->ForEveryPage(
                [](vm_page*& p, uint64_t) {
                    pmm_page_queues().RemoveIfQueued(p);
                    return ZX_ERR_NEXT;
                },
                node->offset(), node->offset() + PAGE_SIZE * VmPageListNode::kPageFanOut);
            res.middle_.insert(ktl::move(node));
        }
        offset += (PAGE_SIZE * VmPageListNode::kPageFanOut);
    }

    // Move any remaining pages into the splice list tail_ node.
    while (offset < end) {
        vm_page_t* page;
        if (RemovePage(offset, &page)) {
            res.tail_.AddPage(page, offset_to_node_index(offset));
        }
        offset += PAGE_SIZE;
    }

    return res;
}

VmPageSpliceList::VmPageSpliceList() : VmPageSpliceList(0, 0) {}

VmPageSpliceList::VmPageSpliceList(uint64_t offset, uint64_t length)
    : offset_(offset), length_(length) {}

VmPageSpliceList::VmPageSpliceList(VmPageSpliceList&& other)
    : offset_(other.offset_), length_(other.length_),
      pos_(other.pos_), middle_(ktl::move(other.middle_)) {
    move_vm_page_list_node(&head_, &other.head_);
    move_vm_page_list_node(&tail_, &other.tail_);

    other.offset_ = other.length_ = other.pos_ = 0;
}

VmPageSpliceList& VmPageSpliceList::operator=(VmPageSpliceList&& other) {
    FreeAllPages();

    offset_ = other.offset_;
    length_ = other.length_;
    pos_ = other.pos_;
    move_vm_page_list_node(&head_, &other.head_);
    move_vm_page_list_node(&tail_, &other.tail_);
    middle_ = ktl::move(other.middle_);

    other.offset_ = other.length_ = other.pos_ = 0;

    return *this;
}

VmPageSpliceList::~VmPageSpliceList() {
    FreeAllPages();
}

void VmPageSpliceList::FreeAllPages() {
    // Free any pages owned by the splice list.
    while (!IsDone()) {
        auto page = Pop();
        if (page) {
            pmm_free_page(page);
        }
    }
}

vm_page* VmPageSpliceList::Pop() {
    if (IsDone()) {
        DEBUG_ASSERT_MSG(false, "Popped from empty splice list");
        return nullptr;
    }

    const uint64_t cur_offset = offset_ + pos_;
    const auto cur_node_idx = offset_to_node_index(cur_offset);
    const auto cur_node_offset = offset_to_node_offset(cur_offset);

    vm_page* res;
    if (offset_to_node_index(offset_) != 0
            && offset_to_node_offset(offset_) == cur_node_offset) {
        // If the original offset means that pages were placed in head_
        // and the current offset points to the same node, look there.
        res = head_.RemovePage(cur_node_idx);
    } else if (cur_node_offset != offset_to_node_offset(offset_ + length_)) {
        // If the current offset isn't pointing to the tail node,
        // look in the middle tree.
        auto middle_node = middle_.find(cur_node_offset);
        if (middle_node.IsValid()) {
            res = middle_node->RemovePage(cur_node_idx);
        } else {
            res = nullptr;
        }
    } else {
        // If none of the other cases, we're in the tail_.
        res = tail_.RemovePage(cur_node_idx);
    }

    pos_ += PAGE_SIZE;
    return res;
}
