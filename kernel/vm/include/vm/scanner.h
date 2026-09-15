// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_

// The 2018 scanner is a thin driver that exposes the `scanner` kernel console
// command, enables the evictor at init time, and starts the page queue aging
// and lru threads. Unlike upstream it has no accessed-bit harvesting thread,
// and eviction itself is triggered synchronously via the console command (see
// scanner.cpp). No public API is required beyond the console command and the
// init hook registered in scanner.cpp.

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
