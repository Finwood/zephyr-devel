/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "status_led.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define LED_GREEN_NODE DT_ALIAS(led_green)
#define LED_RED_NODE   DT_ALIAS(led_red)

BUILD_ASSERT(DT_NODE_EXISTS(LED_GREEN_NODE), "alias led-green missing");
BUILD_ASSERT(DT_NODE_EXISTS(LED_RED_NODE), "alias led-red missing");

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

int hil_status_leds_init(void)
{
	if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0) {
		return -EIO;
	}

	return 0;
}

void hil_status_leds_off(void)
{
	(void)gpio_pin_set_dt(&led_green, 0);
	(void)gpio_pin_set_dt(&led_red, 0);
}

void hil_status_leds_pass(void)
{
	(void)gpio_pin_set_dt(&led_red, 0);
	(void)gpio_pin_set_dt(&led_green, 1);
}

void hil_status_leds_fail(void)
{
	(void)gpio_pin_set_dt(&led_green, 0);
	(void)gpio_pin_set_dt(&led_red, 1);
}
