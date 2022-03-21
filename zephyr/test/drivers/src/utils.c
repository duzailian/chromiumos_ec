/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <shell/shell_uart.h>
#include <drivers/gpio/gpio_emul.h>

#include "battery.h"
#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "power.h"
#include "stubs.h"
#include "chipset.h"
#include "utils.h"

#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))
#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

void test_set_chipset_to_s0(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	const struct device *battery_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	printk("%s: Forcing power on\n", __func__);
	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/*
	 * Make sure that battery is in good condition to
	 * not trigger hibernate in charge_state_v2.c
	 * Set battery voltage to expected value and capacity to 75%. Battery
	 * will not be full and accepts charging, but will not trigger
	 * hibernate. Charge level is chosen arbitrary.
	 */
	bat->cap = bat->full_cap * 3 / 4;
	bat->volt = battery_get_info()->voltage_normal;
	bat->design_mv = bat->volt;

	/* Set battery present gpio. */
	zassert_ok(gpio_emul_input_set(battery_gpio_dev,
				       GPIO_BATT_PRES_ODL_PORT, 0),
		   NULL);

	/* The easiest way to power on seems to be the shell command. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power on"),
		      NULL);

	k_sleep(K_SECONDS(1));

	/* Check if chipset is in correct state */
	zassert_equal(POWER_S0, power_get_state(), "Expected S0, got %d",
		      power_get_state());
}

void test_set_chipset_to_g3(void)
{
	printk("%s: Forcing shutdown\n", __func__);
	chipset_force_shutdown(CHIPSET_RESET_KB_SYSRESET);
	k_sleep(K_SECONDS(20));
	/* Check if chipset is in correct state */
	zassert_equal(POWER_G3, power_get_state(), "Expected G3, got %d",
		      power_get_state());
}

void connect_source_to_port(struct tcpci_src_emul *src, int pdo_index,
			    const struct emul *tcpci_emul,
			    const struct emul *charger_emul)
{
	set_ac_enabled(true);
	zassume_ok(tcpci_src_emul_connect_to_tcpci(&src->data,
						   &src->common_data, &src->ops,
						   tcpci_emul),
		   NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src->data.pdo[pdo_index]));

	k_sleep(K_SECONDS(10));
}

void disconnect_source_from_port(const struct emul *tcpci_emul,
				 const struct emul *charger_emul)
{
	set_ac_enabled(false);
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

void host_cmd_motion_sense_dump(int max_sensor_count,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DUMP,
		.dump = {
			.max_sensor_count = max_sensor_count,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 4, *response, params);

	zassume_ok(host_command_process(&args),
		   "Failed to get motion_sense dump");
}
