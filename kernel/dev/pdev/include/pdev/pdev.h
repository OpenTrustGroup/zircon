// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/boot/image.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// called at platform early init time
// pointer to ZBI containing boot items for kernel drivers to load
void pdev_init(const zbi_header_t* zbi);

__END_CDECLS
