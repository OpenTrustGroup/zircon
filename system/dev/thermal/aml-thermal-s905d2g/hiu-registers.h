// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace thermal {

class SysCpuClkControl0 : public hwreg::RegisterBase<SysCpuClkControl0, uint32_t> {
public:
    DEF_BIT(29, busy_cnt);
    DEF_BIT(28, busy);
    DEF_BIT(26, dyn_enable);
    DEF_FIELD(25, 20, mux1_divn_tcnt);
    DEF_BIT(18, postmux1);
    DEF_FIELD(17, 16, premux1);
    DEF_BIT(15, manual_mux_mode);
    DEF_BIT(14, manual_mode_post);
    DEF_BIT(13, manual_mode_pre);
    DEF_BIT(12, force_update_t);
    DEF_BIT(11, final_mux_sel);
    DEF_BIT(10, final_dyn_mux_sel);
    DEF_FIELD(9, 4, mux0_divn_tcnt);
    DEF_BIT(3, rev);
    DEF_BIT(2, postmux0);
    DEF_FIELD(1, 0, premux0);

    static auto Get() { return hwreg::RegisterAddr<SysCpuClkControl0>(0x19C); }
};

} // namespace thermal
