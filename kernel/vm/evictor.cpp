// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vm/evictor.h>

#include <fbl/ref_ptr.h>
#include <ktl/optional.h>
#include <vm/page_queues.h>
#include <vm/pmm.h>
#include <vm/vm_object_paged.h>

Evictor& pmm_evictor() {
  // The (currently) one and only evictor instance, mirroring the pmm_page_queues()
  // singleton style in page_queues.cpp.
  static Evictor g_evictor;
  return g_evictor;
}

bool Evictor::IsEvictionEnabled() const {
  Guard<Mutex> guard{&lock_};
  return eviction_enabled_;
}

void Evictor::EnableEviction() {
  Guard<Mutex> guard{&lock_};
  eviction_enabled_ = true;
}

void Evictor::DisableEviction() {
  Guard<Mutex> guard{&lock_};
  eviction_enabled_ = false;
}

uint64_t Evictor::EvictOneShotSynchronous(uint64_t min_pages_to_free, EvictionLevel level) {
  if (!IsEvictionEnabled()) {
    return 0;
  }
  // Eviction is synchronous in the 2018 port: evict inline and return the count.
  EvictedPageCounts counts = EvictPagerBacked(min_pages_to_free, level);
  return counts.pager_backed;
}

Evictor::EvictedPageCounts Evictor::EvictPagerBacked(uint64_t target_pages,
                                                     EvictionLevel level) const {
  EvictedPageCounts counts = {};

  // The sole caller, EvictOneShotSynchronous, already checked IsEvictionEnabled(), so it is not
  // re-checked here (the flag could change concurrently regardless).

  // Choose the youngest queue age that is eligible for eviction. |lowest_queue|
  // is passed to PeekPagerBacked() as the youngest age it will accept. For
  // OnlyOldest we restrict to the oldest queues (where DONT_NEED-hinted pages
  // live); for IncludeNewest we accept everything except the active queues, to
  // avoid thrashing the most recently used pages.
  const size_t lowest_evict_queue = level == EvictionLevel::IncludeNewest
                                        ? PageQueues::kNumActiveQueues
                                        : PageQueues::kNumPagerBacked - PageQueues::kNumOldestQueues;

  // [PORT-BLOCKED] Upstream batches evicted pages into a freed_list and frees
  // them in one pmm_node_->FreeList() call, and stack-owns loaned pages across
  // the interval. The 2018 tree has no loaned pages, so we free each page
  // individually with pmm_free_page().
  while (counts.pager_backed < target_pages) {
    ktl::optional<PageQueues::VmoBacklink> backlink =
        pmm_page_queues().PeekPagerBacked(lowest_evict_queue);
    if (!backlink) {
      // No more evictable pages.
      break;
    }
    // A null vmo means the VMO is running its destructor; skip it. The next
    // Peek will make forward progress because the destructing VMO removes its
    // pages from the queues.
    if (!backlink->vmo) {
      continue;
    }
    if (backlink->vmo->RemovePageForEviction(backlink->page, backlink->offset,
                                             VmObjectPaged::EvictionHintAction::Follow)) {
      pmm_free_page(backlink->page);
      counts.pager_backed++;
    }
  }

  return counts;
}
