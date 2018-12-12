/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Raw keyboard I/O layer for MCHP MEC
 */

#include "gpio.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "tfdp_chip.h"

/*
 * Using direct mode interrupt, do not enable
 * GIRQ bit in aggregator block enable register.
 */
void keyboard_raw_init(void)
{
	/* clear key scan PCR sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_KEYSCAN);

	keyboard_raw_enable_interrupt(0);
	gpio_config_module(MODULE_KEYBOARD_SCAN, 1);

	/* Enable keyboard scan interrupt */
	MCHP_INT_ENABLE(MCHP_KS_GIRQ) = MCHP_KS_GIRQ_BIT;
	MCHP_KS_KSI_INT_EN = 0xff;
}

void keyboard_raw_task_start(void)
{
	task_enable_irq(MCHP_IRQ_KSC_INT);
}

test_mockable void keyboard_raw_drive_column(int out)
{
	if (out == KEYBOARD_COLUMN_ALL) {
		MCHP_KS_KSO_SEL = 1 << 5; /* KSEN=0, KSALL=1 */
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
		gpio_set_level(GPIO_KBD_KSO2, 1);
#endif
	} else if (out == KEYBOARD_COLUMN_NONE) {
		MCHP_KS_KSO_SEL = 1 << 6; /* KSEN=1 */
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
		gpio_set_level(GPIO_KBD_KSO2, 0);
#endif
	} else {
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
		if (out == 2) {
			MCHP_KS_KSO_SEL = 1 << 6; /* KSEN=1 */
			gpio_set_level(GPIO_KBD_KSO2, 1);
		} else {
			MCHP_KS_KSO_SEL = out + CONFIG_KEYBOARD_KSO_BASE;
			gpio_set_level(GPIO_KBD_KSO2, 0);
		}
#else
		MCHP_KS_KSO_SEL = out + CONFIG_KEYBOARD_KSO_BASE;
#endif
	}
}

test_mockable int keyboard_raw_read_rows(void)
{
	uint8_t b1, b2;

	b1 = MCHP_KS_KSI_INPUT;
	b2 = (b1 & 0xff) ^ 0xff;

	/* Invert it so 0=not pressed, 1=pressed */
	/* return (MCHP_KS_KSI_INPUT & 0xff) ^ 0xff; */
	return b2;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		MCHP_INT_SOURCE(MCHP_KS_GIRQ) = MCHP_KS_GIRQ_BIT;
		task_clear_pending_irq(MCHP_IRQ_KSC_INT);
		task_enable_irq(MCHP_IRQ_KSC_INT);
	} else {
		task_disable_irq(MCHP_IRQ_KSC_INT);
	}
}

void keyboard_raw_interrupt(void)
{
	/* Clear interrupt status bits */
	MCHP_KS_KSI_STATUS = 0xff;

	MCHP_INT_SOURCE(MCHP_KS_GIRQ) = MCHP_KS_GIRQ_BIT;

	/* Wake keyboard scan task to handle interrupt */
	task_wake(TASK_ID_KEYSCAN);
}
DECLARE_IRQ(MCHP_IRQ_KSC_INT, keyboard_raw_interrupt, 1);

#ifdef CONFIG_KEYBOARD_FACTORY_TEST

/* Run keyboard factory testing, scan out KSO/KSI if any shorted. */
int keyboard_factory_test_scan(void)
{
	int i, j, flags;
	uint16_t shorted = 0;
	uint32_t port, id, val;

	/* Disable keyboard scan while testing */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_CLOSED);

	flags = gpio_get_default_flags(GPIO_KBD_KSO2);

	/* Set all of KSO/KSI pins to internal pull-up and input */
	for (i = 0; i < keyboard_factory_scan_pins_used; i++) {

		if (keyboard_factory_scan_pins[i][0] < 0)
			continue;

		port = keyboard_factory_scan_pins[i][0];
		id = keyboard_factory_scan_pins[i][1];

		gpio_set_alternate_function(port, 1 << id, -1);
		gpio_set_flags_by_mask(port, 1 << id,
			GPIO_INPUT | GPIO_PULL_UP);
	}

	/*
	 * Set start pin to output low, then check other pins
	 * going to low level, it indicate the two pins are shorted.
	 */
	for (i = 0; i < keyboard_factory_scan_pins_used; i++) {

		if (keyboard_factory_scan_pins[i][0] < 0)
			continue;

		port = keyboard_factory_scan_pins[i][0];
		id = keyboard_factory_scan_pins[i][1];

		gpio_set_flags_by_mask(port, 1 << id, GPIO_OUT_LOW);

		for (j = 0; j < i; j++) {

			if (keyboard_factory_scan_pins[j][0] < 0)
				continue;

			/*
			 * Get gpio pin control register,
			 * bit 24 indicate GPIO input from the pad.
			 */
			val = MCHP_GPIO_CTL(keyboard_factory_scan_pins[j][0],
					keyboard_factory_scan_pins[j][1]);

			if ((val & (1 << 24)) == 0) {
				shorted = i << 8 | j;
				goto done;
			}
		}
		gpio_set_flags_by_mask(port, 1 << id,
			GPIO_INPUT | GPIO_PULL_UP);
	}
done:
	gpio_config_module(MODULE_KEYBOARD_SCAN, 1);
	gpio_set_flags(GPIO_KBD_KSO2, flags);
	keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_CLOSED);

	return shorted;
}
#endif
