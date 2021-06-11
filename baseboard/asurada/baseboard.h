/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Asurada board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* IT81202-bx config */
/*
 * NOTE: we need to make correct VCC voltage selection here if EC's VCC isn't
 * connect to 1.8v on other versions.
 */
#define CONFIG_IT83XX_VCC_1P8V

/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. This config will
 * allow the second reset to be treated as a power-on.
 */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_CHIPSET_MT8192
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HIBERNATE_WAKE_PINS_DYNAMIC
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* Chipset */
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_CMD_POWERINDEBUG
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_COMMON
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_WP_ACTIVE_HIGH

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_ODL
#define CONFIG_BATTERY_SMART

/* BC12 */
#define CONFIG_BC12_DETECT_MT6360
#define CONFIG_BC12_DETECT_PI3USB9201
#undef CONFIG_BC12_SINGLE_DRIVER
#define CONFIG_USB_CHARGER

/* Charger */
#define ADC_AMON_BMON ADC_CHARGER_AMON_R /* ADC name remap */
#define ADC_PSYS ADC_CHARGER_PMON /* ADC name remap */
#define ADC_VBUS ADC_VBUS_C0 /* ADC name remap */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_ISL9238C
#define CONFIG_CHARGER_MAINTAIN_VBAT
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_OTG
#define CONFIG_CHARGER_PSYS
#define CONFIG_CHARGER_PSYS_READ
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* BOARD_RS2 */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* BOARD_RS1 */
#define CONFIG_CMD_CHARGER_ADC_AMON_BMON

/* Keyboard */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_USE_GPIO

/* I2C */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_I2C_VIRTUAL_BATTERY
#define I2C_PORT_CHARGER IT83XX_I2C_CH_A
#define I2C_PORT_BATTERY IT83XX_I2C_CH_A
#define I2C_PORT_ACCEL IT83XX_I2C_CH_B
#define I2C_PORT_PPC0  IT83XX_I2C_CH_C
#define I2C_PORT_PPC1  IT83XX_I2C_CH_E
#define I2C_PORT_USB_MUX0 IT83XX_I2C_CH_C
#define I2C_PORT_USB_MUX1 IT83XX_I2C_CH_E
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY
#define CONFIG_SMBUS_PEC

/* LED */
#define CONFIG_LED_COMMON

/* PD / USB-C / PPC */
#define CONFIG_CMD_PPC_DUMP
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_IT83XX_TUNE_CC_PHY
#define CONFIG_USB_MUX_VIRTUAL
#define CONFIG_USBC_PPC
#define CONFIG_USBC_PPC_DEDICATED_INT
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_SYV682C
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_MUX_IT5205 /* C0 */
#define CONFIG_USB_MUX_PS8743 /* C1 */
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_PD_DISCHARGE
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DP_HPD_GPIO
#define CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_FRS_PPC
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 2
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
#define CONFIG_USB_PID 0x5053
#define CONFIG_USB_POWER_DELIVERY

/* USB-A */
#define CONFIG_USB_PORT_POWER_DUMB
#define CONFIG_USB_PORT_POWER_DUMB_CUSTOM_HOOK
#define USB_PORT_COUNT USBA_PORT_COUNT

/* UART */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Sensor */
#ifdef HAS_TASK_MOTIONSENSE
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_ACCELS

#define CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 256
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#define CONFIG_ACCEL_INTERRUPTS
#endif

/* SPI / Host Command */
#define CONFIG_SPI
#define CONFIG_SUPPRESSED_HOST_COMMANDS \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ, EC_CMD_MOTION_SENSE_CMD, \
	EC_CMD_PD_GET_LOG_ENTRY

/* MKBP */
#define CONFIG_MKBP_EVENT

#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_USE_GPIO

/* Voltage regulator control */
#define CONFIG_HOSTCMD_REGULATOR

/* Define the host events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK                   \
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |    \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) | \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |        \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT) |     \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE) | \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON))

/* GPIO name remapping */
#define GPIO_EN_HDMI_PWR        GPIO_EC_X_GPIO1
#define GPIO_USB_C1_FRS_EN      GPIO_EC_X_GPIO1
#define GPIO_USB_C1_PPC_INT_ODL GPIO_X_EC_GPIO2
#define GPIO_PS185_EC_DP_HPD    GPIO_X_EC_GPIO2
#define GPIO_USB_C1_DP_IN_HPD   GPIO_EC_X_GPIO3
#define GPIO_PS185_PWRDN_ODL    GPIO_EC_X_GPIO3

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum power_signal {
	PMIC_PWR_GOOD,
	AP_IN_S3_L,
	AP_WDT_ASSERTED,
	POWER_SIGNAL_COUNT,
};

enum board_sub_board {
	SUB_BOARD_NONE = -1,
	SUB_BOARD_TYPEC,
	SUB_BOARD_HDMI,
	SUB_BOARD_COUNT,
};

int board_get_version(void);
void board_reset_pd_mcu(void);
enum board_sub_board board_get_sub_board(void);
void usb_a0_interrupt(enum gpio_signal signal);
#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_H */
