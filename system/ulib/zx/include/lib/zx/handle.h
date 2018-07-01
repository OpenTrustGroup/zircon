// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/object.h>

namespace zx {

using handle = object<void>;
using unowned_handle = unowned<handle>;

} // namespace zx
