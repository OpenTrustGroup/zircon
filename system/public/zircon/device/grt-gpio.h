// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The control device of the GPIO driver.
#define GRT_GPIO_CONTROL_DEVICE "/dev/sys/platform/gpio/mtk_gpio"

// Read the input level of a pin. The input is the pin number and the output
// the level, 0 or 1, each one uint32_t.
#define IOCTL_GRT_GET_GPIO_LEVEL \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPIO, 0)

// Disable the GPIO interrupt and remove the device once the guest owns the
// GPIO controller.
#define IOCTL_GRT_RELEASE_GPIO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPIO, 1)

// Return the event the driver signals with ZX_USER_SIGNAL_0 on a GPIO
// interrupt.
#define IOCTL_GRT_GPIO_GET_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_GPIO, 2)

// Enable or disable the GPIO interrupt. The input is one uint32_t,
// GRT_GPIO_IRQ_ENABLE or GRT_GPIO_IRQ_DISABLE.
#define IOCTL_GRT_GPIO_ENABLE_IRQ \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPIO, 3)

#define GRT_GPIO_IRQ_DISABLE 0u
#define GRT_GPIO_IRQ_ENABLE 1u

// ssize_t ioctl_grt_get_gpio_level(int fd, const uint32_t* pin, uint32_t* level);
IOCTL_WRAPPER_INOUT(ioctl_grt_get_gpio_level, IOCTL_GRT_GET_GPIO_LEVEL, uint32_t, uint32_t);

// ssize_t ioctl_grt_release_gpio(int fd);
IOCTL_WRAPPER(ioctl_grt_release_gpio, IOCTL_GRT_RELEASE_GPIO);

// ssize_t ioctl_grt_gpio_get_event(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_grt_gpio_get_event, IOCTL_GRT_GPIO_GET_EVENT, zx_handle_t);

// ssize_t ioctl_grt_gpio_enable_irq(int fd, const uint32_t* enable);
IOCTL_WRAPPER_IN(ioctl_grt_gpio_enable_irq, IOCTL_GRT_GPIO_ENABLE_IRQ, uint32_t);

__END_CDECLS
