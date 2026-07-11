// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <kernel/lockdep.h>
#include <kernel/mutex.h>

// Implements page eviction logic to free pager-backed pages under memory
// pressure. This is a heavily trimmed 2018 port of the upstream F7
// vm/evictor.cc: only the one-shot synchronous pager-backed path is kept, since
// the 2018 scanner drives eviction inline from a console command and the test
// polls for the result.
//
// [PORT-BLOCKED] The upstream Evictor also implements continuous/asynchronous
// eviction on a dedicated eviction thread (EvictionThreadLoop,
// EvictOneShotAsynchronous, EnableContinuousEviction), OOM free-memory targets
// (EvictUntilTargetsMet), discardable-VMO reclaim (EvictDiscardable), and
// loaned-page accounting. None of that infrastructure exists in the 2018 tree,
// so all of it is cut. The scanner calls EvictOneShotSynchronous() directly.
//
// This class is thread-safe.
class Evictor {
 public:
  // A rough control on how old a page must be before it is considered for
  // eviction. OnlyOldest restricts eviction to the oldest pager-backed queues
  // (where DONT_NEED-hinted pages live), while IncludeNewest permits eviction
  // from all but the most-recently-active queues.
  enum class EvictionLevel : uint8_t {
    OnlyOldest = 0,
    IncludeNewest = 1,
  };

  // The number of pager-backed pages evicted by an operation.
  struct EvictedPageCounts {
    uint64_t pager_backed = 0;
  };

  Evictor() = default;
  ~Evictor() = default;

  Evictor(const Evictor&) = delete;
  Evictor& operator=(const Evictor&) = delete;

  // Called from the scanner init hook to allow eviction to occur.
  void EnableEviction();
  // Disable all eviction. Since eviction is synchronous there is no thread to
  // shut down; this just flips the enabled flag.
  void DisableEviction();
  // Whether eviction can occur.
  bool IsEvictionEnabled() const;

  // Perform a synchronous, one-shot eviction of at least |min_pages_to_free|
  // pager-backed pages. Returns the number of pages actually evicted. The
  // |level| controls how old a page must be to be eligible. This may acquire
  // arbitrary vmo locks.
  uint64_t EvictOneShotSynchronous(uint64_t min_pages_to_free, EvictionLevel level);

 private:
  // Evict up to |target_pages| pager-backed pages at the requested |level|,
  // freeing each evicted page back to the pmm. Returns the number evicted.
  EvictedPageCounts EvictPagerBacked(uint64_t target_pages, EvictionLevel level) const;

  DECLARE_MUTEX(Evictor) mutable lock_;

  // Whether eviction is enabled. Guarded by |lock_|.
  bool eviction_enabled_ TA_GUARDED(lock_) = false;
};

// The (currently) one and only evictor instance, mirroring the pmm_page_queues()
// singleton style in page_queues.cpp.
Evictor& pmm_evictor();

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_
