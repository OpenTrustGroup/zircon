// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/algorithm.h>
#include <ktl/atomic.h>
#include <list.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>

enum vm_page_state {
    VM_PAGE_STATE_FREE,
    VM_PAGE_STATE_ALLOC,
    VM_PAGE_STATE_OBJECT,
    VM_PAGE_STATE_WIRED,
    VM_PAGE_STATE_HEAP,
    VM_PAGE_STATE_MMU,   // allocated to serve arch-specific mmu purposes
    VM_PAGE_STATE_IOMMU, // allocated for platform-specific iommu structures

    VM_PAGE_STATE_COUNT_
};

#define VM_PAGE_STATE_BITS 3
static_assert((1u << VM_PAGE_STATE_BITS) >= VM_PAGE_STATE_COUNT_, "");

#define VM_PAGE_PAGER_DIRTY (1 << 0)
#define VM_PAGE_PAGER_CLEANING (1 << 1)

// Forward declaration for the pager-backed page backlink. The 2018 VM has no
// VmCowPages, so a page's owner backlink is the VmObjectPaged directly (upstream
// stores a VmCowPages backlink here).
class VmObjectPaged;

// core per page structure allocated at pmm arena creation time
typedef struct vm_page {
#if WITH_USER_PAGER
    // A pager-backed page is queued and object-owned at the same time: the page
    // queues thread a resident page onto an age or eviction queue while it still
    // carries the owner backlink below. The link therefore lives outside the
    // union.
    struct list_node queue_node;
#endif
    paddr_t paddr_priv; // use paddr() accessor

    struct {
        uint32_t flags : 8;
        uint32_t state : VM_PAGE_STATE_BITS;
    };

    union {
#if !WITH_USER_PAGER
        // Nothing queues a resident page without the pager, so the link is only
        // ever used while the page is free or in transit between the pmm and a
        // VMO, and can share storage with the object fields. The page queues are
        // the only writer of those fields and are not built here, so the storage
        // never holds anything but the link.
        struct list_node queue_node;
#endif
        struct {
#define VM_PAGE_OBJECT_PIN_COUNT_BITS 5
#define VM_PAGE_OBJECT_MAX_PIN_COUNT ((1ul << VM_PAGE_OBJECT_PIN_COUNT_BITS) - 1)

            // Backlink to the owning VmObjectPaged and the page's offset within it.
            // Set when the page is inserted into a pager-backed VMO's page list; the
            // page queues / evictor use it to locate and reclaim the page. Upstream
            // stores a VmCowPages backlink here; 2018 has no VmCowPages, so this is the
            // VmObjectPaged directly. Null for pages not owned by a paged VMO.
            VmObjectPaged* object_priv;
            uint64_t page_offset_priv;

            // Current PageQueues bucket (a PageQueue enum value). Updated as the page
            // ages / is hinted; read to select eviction candidates. Stored as an
            // atomic byte because the page queues update and read it both under the
            // page-queues lock and on a lockless path (MarkAccessedDeferredCount),
            // matching upstream which holds this in an atomic. 2018 lacks
            // std::atomic_ref, so this is a full ktl::atomic (== fbl::atomic) member;
            // it is 1 byte like the plain uint8_t it replaces, so the layout is
            // unchanged.
            ktl::atomic<uint8_t> page_queue_priv;

            uint8_t pin_count : VM_PAGE_OBJECT_PIN_COUNT_BITS;
            // Sticky ZX_VMO_OP_ALWAYS_NEED hint: prefer to keep this page resident;
            // eviction skips it unless overridden under memory pressure.
            uint8_t always_need : 1;

            // Accessors mirroring upstream's vm_page.object surface.
            VmObjectPaged* get_object() const { return object_priv; }
            void set_object(VmObjectPaged* obj) { object_priv = obj; }
            void clear_object() {
                object_priv = nullptr;
                page_offset_priv = 0;
            }
            uint64_t get_page_offset() const { return page_offset_priv; }
            void set_page_offset(uint64_t offset) { page_offset_priv = offset; }
            // Returns a reference to the atomic queue index so callers can use the
            // full atomic surface (load/store/exchange/compare_exchange_weak). The
            // const overload returns a const reference; loads on it must use the
            // const load() overload.
            ktl::atomic<uint8_t>& get_page_queue_ref() { return page_queue_priv; }
            const ktl::atomic<uint8_t>& get_page_queue_ref() const { return page_queue_priv; }
        } object; // attached to a vm object
    };

    // helper routines
    bool is_free() const {
        return state == VM_PAGE_STATE_FREE;
    }

    void dump() const;

    // return the physical address
    // future plan to store in a compressed form
    paddr_t paddr() const { return paddr_priv; }
} vm_page_t;

// assert that the page structure isn't growing uncontrollably. Grown from 0x20 to
// hold the pager-backed backlink/offset/queue fields the page queues need.
#if WITH_USER_PAGER
static_assert(sizeof(vm_page) == 0x38, "");
#else
// Keeping the queue link in the union buys back the 16 bytes it costs above.
static_assert(sizeof(vm_page) == 0x28, "");
#endif

// helpers
const char* page_state_to_string(unsigned int state);
