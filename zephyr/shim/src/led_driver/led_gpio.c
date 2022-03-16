/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * GPIO LED control.
 */

#include "led.h"
#include "util.h"

#include <devicetree.h>
#include <drivers/gpio.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(gpio_led, LOG_LEVEL_ERR);

#define GPIO_LED_PINS_NODE	DT_PATH(gpio_led_pins)
#define LED_PIN_COUNT		(LED_COLOR_COUNT - 1)

/*
 * Struct defining LED GPIO pin and value to set.
 */
struct gpio_pin_t {
	enum gpio_signal signal;
	int val;
};

/*
 * Pin node contains LED color and array of GPIO pins
 * to alter in order to enable the given color.
 */
struct led_pins_node_t {
	int led_color;
	struct gpio_pin_t gpio_pins[LED_PIN_COUNT];
};

#define SET_PIN(node_id, prop, i)					\
{									\
	.signal = GPIO_SIGNAL(DT_PHANDLE_BY_IDX(node_id, prop, i)),	\
	.val = DT_PHA_BY_IDX(node_id, prop, i, value)			\
},

#define SET_GPIO_PIN(node_id)						\
{									\
	DT_FOREACH_PROP_ELEM(node_id, led_pins, SET_PIN)		\
}

#define SET_PIN_NODE(node_id)						\
{									\
	.led_color = GET_PROP(node_id, led_color),			\
	.gpio_pins = SET_GPIO_PIN(node_id)				\
},

struct led_pins_node_t pins_node[LED_COLOR_COUNT] = {
	DT_FOREACH_CHILD(GPIO_LED_PINS_NODE, SET_PIN_NODE)
};

/*
 * Iterate through LED pins nodes to find the color matching node.
 * Set all the GPIO pins defined in the node to the defined value,
 * to enable the color.
 */
void led_set_color(enum led_color color)
{
	for (int i = 0; i < LED_COLOR_COUNT; i++) {
		if (pins_node[i].led_color == color) {
			for (int j = 0; j < LED_PIN_COUNT; j++) {
				gpio_pin_set_dt(gpio_get_dt_spec(
					pins_node[i].gpio_pins[j].signal),
					pins_node[i].gpio_pins[j].val);
			}
		}
	}
}