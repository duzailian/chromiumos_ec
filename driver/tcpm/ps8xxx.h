/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parade Tech Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_PS8XXX_H
#define __CROS_EC_USB_PD_TCPM_PS8XXX_H

/* I2C interface */
#define PS8751_I2C_ADDR1_P1       0x12
#define PS8751_I2C_ADDR1          0x16
#define PS8751_I2C_ADDR2          0x36
#define PS8751_I2C_ADDR3          0x56
#define PS8751_I2C_ADDR4          0x96

#define PS8751_P3_TO_P1(p3)	((p3) - 4)

/* Minimum Delay for reset assertion */
#define PS8XXX_RESET_DELAY_MS 1

#define PS8751_BIST_TIMER_FREQ  15000000
#define PS8751_BIST_DELAY_MS    50

#define PS8751_BIST_COUNTER (PS8751_BIST_TIMER_FREQ / MSEC \
				* PS8751_BIST_DELAY_MS)

#define PS8751_BIST_COUNTER_BYTE0 (PS8751_BIST_COUNTER & 0xff)
#define PS8751_BIST_COUNTER_BYTE1 ((PS8751_BIST_COUNTER >> 8) & 0xff)
#define PS8751_BIST_COUNTER_BYTE2 ((PS8751_BIST_COUNTER >> 16) & 0xff)

#define PS8XXX_VENDOR_ID  0x1DA0
#define PS8XXX_REG_I2C_DEBUGGING_ENABLE         0xA0
#define PS8XXX_REG_I2C_DEBUGGING_ENABLE_ON      0x30
#define PS8XXX_REG_I2C_DEBUGGING_ENABLE_OFF     0x31    /* default */
#define PS8XXX_REG_BIST_CONT_MODE_BYTE0         0xBC
#define PS8XXX_REG_BIST_CONT_MODE_BYTE1         0xBD
#define PS8XXX_REG_BIST_CONT_MODE_BYTE2         0xBE
#define PS8XXX_REG_BIST_CONT_MODE_CTR           0xBF
#define PS8XXX_REG_DET_CTRL0                    0x08

#define PS8XXX_REG_MUX_USB_DCI_CFG_MODE_MASK    0xC0
#define PS8XXX_REG_MUX_USB_DCI_CFG_MODE_OFF     0x80

#define MUX_IN_HPD_ASSERTION_REG                0xD0
#define IN_HPD  (1 << 0)
#define HPD_IRQ (1 << 1)

#define PS8XXX_P1_REG_MUX_USB_DCI_CFG           0x4B

/* NOTE: The Product ID will read as 0x8803 if the firmware has malfunctioned in
 * 8705, 8755 and 8805.
 */
#define PS8705_PRODUCT_ID 0x8705
#define PS8751_PRODUCT_ID 0x8751
#define PS8755_PRODUCT_ID 0x8755
#define PS8805_PRODUCT_ID 0x8805
#define PS8815_PRODUCT_ID 0x8815

#if defined(CONFIG_USB_PD_TCPM_PS8751)
/* Vendor defined registers */
#define PS8XXX_REG_VENDOR_ID_L                  0x00
#define PS8XXX_REG_VENDOR_ID_H                  0x01
#define PS8XXX_REG_MUX_DP_EQ_CONFIGURATION      0xD3
#define PS8XXX_REG_MUX_DP_OUTPUT_CONFIGURATION  0xD4
#define PS8XXX_REG_MUX_USB_C2SS_EQ              0xE7
#define PS8XXX_REG_MUX_USB_C2SS_HS_THRESHOLD    0xE8
#define PS8751_REG_MUX_USB_DCI_CFG              0xED
#endif

#if defined(CONFIG_USB_PD_TCPM_PS8815)
/* Vendor defined registers */
#define PS8815_P1_REG_HW_REVISION		0xF0

#endif

extern const struct tcpm_drv ps8xxx_tcpm_drv;
void ps8xxx_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq);

/**
 * Board specific callback to judge and provide which chip source of PS8XXX
 * series supported by this driver per specific port.
 *
 * If the board supports only one single source then there is no nencessary to
 * provide the __override version.
 *
 * If board supports two sources or above (with CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
 * then the __override version is mandatory.
 *
 * @param port	TCPC port number.
 */
__override_proto
uint16_t board_get_ps8xxx_product_id(int port);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
extern struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev;
#endif /* defined(CONFIG_CMD_I2C_STRESS_TEST_TCPC) */

extern const struct usb_mux_driver ps8xxx_usb_mux_driver;

#endif /* defined(__CROS_EC_USB_PD_TCPM_PS8XXX_H) */
