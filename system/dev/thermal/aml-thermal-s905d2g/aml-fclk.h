// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef struct {
    uint32_t rate;
    uint16_t premux;
    uint16_t postmux;
    uint16_t mux_div;
} aml_fclk_rate_table_t;


__BEGIN_CDECLS;

// Return the Fixed clk rate table.
const aml_fclk_rate_table_t* s905d2_fclk_get_rate_table(void);

// Return the rate table count.
size_t s905d2_fclk_get_rate_table_count(void);

__END_CDECLS;
