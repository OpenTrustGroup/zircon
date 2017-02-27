// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/syscalls.h>

typedef mx_status_t (job_callback_t)(int depth, mx_handle_t job, mx_koid_t koid);
typedef mx_status_t (process_callback_t)(int depth, mx_handle_t process, mx_koid_t koid);

mx_status_t walk_process_tree(job_callback_t job_callback, process_callback_t process_callback);
