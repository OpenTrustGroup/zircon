// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Replaces page_queues.cpp when the kernel is built without WITH_USER_PAGER.
//
// The page queues exist to age and reclaim pager-backed pages. Without a user
// pager no page is ever pager-backed, so every queue is permanently empty and
// the classification work on the page insert/remove path is pure overhead. This
// translation unit keeps the PageQueues type -- so vm_page_list.cpp and
// vm_object_paged.cpp compile with no #ifdef in them -- and gives the five
// methods they call empty bodies, which the optimizer then removes from the
// callers.
//
// page_queues.cpp, evictor.cpp and scanner.cpp all leave the build together
// (see rules.mk): the evictor reclaims only what the queues classify, and the
// scanner's sole job is to enable the evictor and to serve the queues' accessed
// scan.

#include <vm/page_queues.h>

#include <vm/pmm.h>

// The members are never read, so nothing here needs initializing. fbl::Mutex
// and the atomics still run their own constructors.
PageQueues::PageQueues() {}

PageQueues::~PageQueues() {}

bool PageQueues::RemoveIfQueued(vm_page_t* page) {
  // Nothing was ever queued.
  return false;
}

void PageQueues::SetUnswappable(vm_page_t* page) {}

void PageQueues::SetPagerBacked(vm_page_t* page, VmObjectPaged* object, uint64_t page_offset) {}

void PageQueues::MoveToPagerBackedDontNeed(vm_page_t* page) {}

void PageQueues::MarkAccessed(vm_page_t* page) {}

// Same singleton style as page_queues.cpp, so the accessor stays a plain load
// rather than gaining a guard variable.
namespace {
PageQueues g_page_queues;
}  // namespace

PageQueues& pmm_page_queues() { return g_page_queues; }
