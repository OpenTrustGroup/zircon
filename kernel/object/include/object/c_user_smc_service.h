// Copyright 2018 Open Trust Group
// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// N.B. This file is included by C code.

__BEGIN_CDECLS

// This is the interface for LK's libsm to notify the
// userspace smc service to handle smc request.
long notify_smc_service(smc32_args_t* args);
long notify_nop_service(smc32_args_t* args);

__END_CDECLS
