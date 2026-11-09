// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <object/slot_interrupt_dispatcher.h>
#include <sys/types.h>

class SlotInterruptEventDispatcher final : public SlotInterruptDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    SlotInterruptEventDispatcher(const SlotInterruptDispatcher &) = delete;
    SlotInterruptEventDispatcher& operator=(const SlotInterruptDispatcher &) = delete;

    zx_status_t Bind(uint32_t slot, uint32_t vector, uint32_t options) final;

protected:
    void MaskInterrupt(uint32_t vector) final;
    void UnmaskInterrupt(uint32_t vector) final;
    zx_status_t RegisterInterruptHandler(uint32_t vector, void* data) final;
    void UnregisterInterruptHandler(uint32_t vector) final;

private:
    explicit SlotInterruptEventDispatcher() {}

    static void IrqHandler(void* ctx);

    fbl::Canary<fbl::magic("INED")> canary_;
};
