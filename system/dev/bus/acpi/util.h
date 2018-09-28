// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

ACPI_STATUS acpi_evaluate_integer(ACPI_HANDLE handle, const char* name, uint64_t* out);
ACPI_STATUS acpi_evaluate_method_intarg(ACPI_HANDLE handle, const char* name, uint64_t arg);

__END_CDECLS;
