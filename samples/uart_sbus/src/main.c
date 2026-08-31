/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "sbus_pipe.h"

#define UART_IN_NODE     DT_ALIAS(uart_in)
#define SBUS_OUT_NODE    DT_ALIAS(sbus_out)
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led1)

#define MAIN_TICK_MS       5
#define GREEN_PULSE_MS     50
#define RED_SUPERSEDE_MS   200
#define RED_FAULT_MS       2000
#define STATS_PERIOD_MS    1000
#define FRAME_LED_DIVISOR  10

BUILD_ASSERT(DT_NODE_EXISTS(UART_IN_NODE), "alias uart-in missing");
BUILD_ASSERT(DT_NODE_EXISTS(SBUS_OUT_NODE), "alias sbus-out missing");
BUILD_ASSERT(DT_NODE_EXISTS(LED_GREEN_NODE), "alias led0 missing");
BUILD_ASSERT(DT_NODE_EXISTS(LED_RED_NODE), "alias led1 missing");
BUILD_ASSERT(DT_IRQ(UART_IN_NODE, priority) == DT_IRQ(SBUS_OUT_NODE, priority),
	     "uart-in and sbus-out must share NVIC priority");

static const struct device *const uart_in = DEVICE_DT_GET(UART_IN_NODE);
static const struct device *const sbus_out = DEVICE_DT_GET(SBUS_OUT_NODE);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

static struct sbus_pipe pipe;
K_SEM_DEFINE(wake, 0, 1);

static void uart_in_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	int err;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	err = uart_err_check(dev);
	if (err != 0) {
		atomic_inc(&pipe.rx_err);
		k_sem_give(&wake);
	}

	while (uart_irq_rx_ready(dev) > 0) {
		if (uart_fifo_read(dev, &c, 1) < 1) {
			break;
		}
		if (sbus_pipe_push(&pipe, c)) {
			uart_irq_tx_enable(sbus_out);
			if (pipe.collect == NULL) {
				k_sem_give(&wake);
			}
		}
	}
}

static void sbus_out_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	while (uart_irq_tx_ready(dev) > 0) {
		if (!sbus_pipe_pop(&pipe, &c)) {
			uart_irq_tx_disable(dev);
			break;
		}
		k_sem_give(&wake);
		if (uart_fifo_fill(dev, &c, 1) < 1) {
			uart_irq_tx_disable(dev);
			break;
		}
	}
}

static uint16_t remaining_sub(uint16_t remaining, uint16_t dt)
{
	return remaining > dt ? remaining - dt : 0;
}

int main(void)
{
	uint32_t tx_frames_prev = 0;
	uint32_t tx_frames_snap = 0;
	uint32_t supersede_snap = 0;
	uint32_t sync_err_snap = 0;
	uint32_t rx_err_snap = 0;
	uint16_t green_ms = 0;
	uint16_t red_ms = 0;
	int64_t last_led = k_uptime_get();
	int64_t last_stats = last_led;

	sbus_pipe_init(&pipe);

	if (!device_is_ready(uart_in) || !device_is_ready(sbus_out)) {
		printk("sbus: UART device not ready\n");
		return 0;
	}
	if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
		printk("sbus: LED GPIO not ready\n");
		return 0;
	}
	if (gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("sbus: LED configure failed\n");
		return 0;
	}

	uart_irq_callback_user_data_set(uart_in, uart_in_cb, NULL);
	uart_irq_callback_user_data_set(sbus_out, sbus_out_cb, NULL);
	uart_irq_err_enable(uart_in);
	uart_irq_rx_enable(uart_in);

	printk("sbus: D0/PA10 115200 8N1 -> D13/PB3 S.BUS 100k 8E2\n");

	for (;;) {
		int64_t now;
		uint16_t dt;
		uint32_t rx;
		uint32_t tx;
		uint32_t err;
		uint32_t frames;
		uint32_t sup;
		uint32_t sync;
		uint32_t tx_frames;

		k_sem_take(&wake, K_MSEC(MAIN_TICK_MS));
		now = k_uptime_get();
		dt = (uint16_t)CLAMP(now - last_led, 0, 1000);
		last_led = now;

		green_ms = remaining_sub(green_ms, dt);
		red_ms = remaining_sub(red_ms, dt);

		tx_frames = (uint32_t)atomic_get(&pipe.tx_frames);
		if ((tx_frames / FRAME_LED_DIVISOR) != (tx_frames_snap / FRAME_LED_DIVISOR)) {
			green_ms = MAX(green_ms, GREEN_PULSE_MS);
		}
		tx_frames_snap = tx_frames;

		sup = (uint32_t)atomic_get(&pipe.supersede);
		if (sup != supersede_snap) {
			red_ms = MAX(red_ms, RED_SUPERSEDE_MS);
		}
		supersede_snap = sup;

		sync = (uint32_t)atomic_get(&pipe.sync_err);
		err = (uint32_t)atomic_get(&pipe.rx_err);
		if (sync != sync_err_snap || err != rx_err_snap) {
			red_ms = MAX(red_ms, RED_FAULT_MS);
		}
		sync_err_snap = sync;
		rx_err_snap = err;

		if (green_ms > 0) {
			gpio_pin_set_dt(&led_green, 1);
		} else {
			gpio_pin_set_dt(&led_green, 0);
		}
		if (red_ms > 0) {
			gpio_pin_set_dt(&led_red, 1);
		} else {
			gpio_pin_set_dt(&led_red, 0);
		}

		if ((now - last_stats) >= STATS_PERIOD_MS) {
			uint32_t fps;

			rx = (uint32_t)atomic_get(&pipe.rx_bytes);
			tx = (uint32_t)atomic_get(&pipe.tx_bytes);
			frames = tx_frames;
			fps = (tx_frames - tx_frames_prev) * MSEC_PER_SEC / (now - last_stats);
			tx_frames_prev = tx_frames;
			last_stats = now;
			printk("sbus: rx=%u tx=%u err=%u frames=%u fps=%u sup=%u sync=%u\n",
			       rx, tx, err, frames, fps, sup, sync);
		}
	}
}
