/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H


#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_ENTERING_RW		GPIO_UNIMPLEMENTED

/* daughterboard GPIO remap */
#define GPIO_EN_HDMI_PWR        GPIO_EC_X_GPIO1
#define GPIO_USB_C1_FRS_EN      GPIO_EC_X_GPIO1
#define GPIO_USB_C1_PPC_INT_ODL GPIO_X_EC_GPIO2
#define GPIO_PS185_EC_DP_HPD    GPIO_X_EC_GPIO2
#define GPIO_USB_C1_DP_IN_HPD   GPIO_EC_X_GPIO3
#define GPIO_PS185_PWRDN_ODL    GPIO_EC_X_GPIO3

#ifdef CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
#define GPIO_EN_USB_C1_SINK         RT1718S_GPIO1
#define GPIO_EN_USB_C1_SOURCE       RT1718S_GPIO2
#define GPIO_EN_USB_C1_FRS          RT1718S_GPIO3
#endif

#endif /* __ZEPHYR_GPIO_MAP_H */
