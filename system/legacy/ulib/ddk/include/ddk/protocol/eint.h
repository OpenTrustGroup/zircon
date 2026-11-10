// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Called from the external interrupt controller's interrupt thread when
// "eint_num" fires. The controller sets "*is_forward" to 1 before the call;
// the handler may change it to tell the controller whether to forward the
// interrupt any further.
typedef void (*eint_irq_handler_t)(void* ctx, uint32_t eint_num, uint32_t* is_forward);

// The protocol an external interrupt (EINT) controller driver registers with
// the platform bus through pbus_set_protocol(ZX_PROTOCOL_EINT, ...), and that
// client drivers obtain with device_get_protocol(ZX_PROTOCOL_EINT, ...).
typedef struct eint_protocol_ops {
    // Configures "eint_num" with the GPIO_TRIGGER_* flags in "type" and calls
    // "handler" with "handler_ctx" each time it fires.
    zx_status_t (*register_irq)(void* ctx, uint32_t eint_num, uint32_t type,
                                eint_irq_handler_t handler, void* handler_ctx);
    // Masks "eint_num" and removes its handler.
    zx_status_t (*unregister_irq)(void* ctx, uint32_t eint_num);
} eint_protocol_ops_t;

typedef struct eint_protocol {
    eint_protocol_ops_t* ops;
    void* ctx;
} eint_protocol_t;

static inline zx_status_t eint_register_irq(eint_protocol_t* eint, uint32_t eint_num,
                                            uint32_t type, eint_irq_handler_t handler,
                                            void* handler_ctx) {
    return eint->ops->register_irq(eint->ctx, eint_num, type, handler, handler_ctx);
}

static inline zx_status_t eint_unregister_irq(eint_protocol_t* eint, uint32_t eint_num) {
    return eint->ops->unregister_irq(eint->ctx, eint_num);
}

__END_CDECLS;
