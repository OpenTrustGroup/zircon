// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <debug.h>
#include <lib/console.h>
#include <string.h>
#include <sys/types.h>

#include <arch/defines.h>
#include <lk/init.h>
#include <vm/evictor.h>
#include <vm/page_queues.h>
#include <vm/pmm.h>
#include <vm/scanner.h>

// Enable synchronous eviction once the VM is up. There is no scanning/aging
// thread in the 2018 port; eviction is driven inline by the `scanner reclaim`
// console command. LK_INIT_LEVEL_LAST ensures the page queues and pmm are fully
// initialized before eviction is permitted.
static void scanner_init_func(uint level) { pmm_evictor().EnableEviction(); }
LK_INIT_HOOK(scanner, &scanner_init_func, LK_INIT_LEVEL_LAST);

static int cmd_scanner(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  usage:
    printf("usage:\n");
    printf("%s dump                    : dump the pager-backed page queues\n", argv[0].str);
    printf("%s rotate_queue            : rotate the pager-backed page queues\n", argv[0].str);
    printf("%s reclaim <MB> [only_old] : evict <MB> of pager-backed memory\n", argv[0].str);
    printf("%s reclaim_all             : evict all evictable pager-backed memory\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    pmm_page_queues().Dump();
  } else if (!strcmp(argv[1].str, "rotate_queue")) {
    pmm_page_queues().RotatePagerBackedQueues();
  } else if (!strcmp(argv[1].str, "reclaim_all")) {
    // Evict everything evictable, including the newest non-active pages.
    uint64_t pages = pmm_evictor().EvictOneShotSynchronous(
        UINT64_MAX, Evictor::EvictionLevel::IncludeNewest);
    printf("Evicted %lu pages\n", pages);
  } else if (!strcmp(argv[1].str, "reclaim")) {
    if (argc < 3) {
      printf("not enough arguments\n");
      goto usage;
    }
    // target_pages = MB * (1 MB / PAGE_SIZE). The optional "only_old" argument
    // restricts eviction to the oldest queues (where DONT_NEED-hinted pages
    // live); otherwise all but the active queues are considered.
    uint64_t target_pages = argv[2].u * (MB / PAGE_SIZE);
    Evictor::EvictionLevel level = Evictor::EvictionLevel::IncludeNewest;
    if (argc >= 4 && !strcmp(argv[3].str, "only_old")) {
      level = Evictor::EvictionLevel::OnlyOldest;
    }
    uint64_t pages = pmm_evictor().EvictOneShotSynchronous(target_pages, level);
    printf("Evicted %lu pages\n", pages);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("scanner", "scanner commands", &cmd_scanner)
#endif
STATIC_COMMAND_END(scanner);
