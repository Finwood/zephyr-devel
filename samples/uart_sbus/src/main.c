/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "sbus_pipe.h"

#define UART_IN_NODE  DT_ALIAS(uart_in)
#define SBUS_OUT_NODE DT_ALIAS(sbus_out)

BUILD_ASSERT(DT_NODE_EXISTS(UART_IN_NODE), "alias uart-in missing");
BUILD_ASSERT(DT_NODE_EXISTS(SBUS_OUT_NODE), "alias sbus-out missing");

static const struct device *const uart_in = DEVICE_DT_GET(UART_IN_NODE);
static const struct device *const sbus_out = DEVICE_DT_GET(SBUS_OUT_NODE);

static struct sbus_pipe pipe;

static void uart_in_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	int err;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	err = uart_err_check(dev);
	if (err != 0) {
		atomic_inc(&pipe.rx_err);
	}

	while (uart_irq_rx_ready(dev) > 0) {
		if (uart_fifo_read(dev, &c, 1) < 1) {
			break;
		}
		if (sbus_pipe_push(&pipe, c)) {
			uart_irq_tx_enable(sbus_out);
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
			return;
		}
		if (uart_fifo_fill(dev, &c, 1) < 1) {
			uart_irq_tx_disable(dev);
			return;
		}
	}
}

int main(void)
{
	uint32_t tx_prev = 0;

	sbus_pipe_init(&pipe);

	if (!device_is_ready(uart_in) || !device_is_ready(sbus_out)) {
		printk("sbus: UART device not ready\n");
		return 0;
	}

	uart_irq_callback_user_data_set(uart_in, uart_in_cb, NULL);
	uart_irq_callback_user_data_set(sbus_out, sbus_out_cb, NULL);
	uart_irq_err_enable(uart_in);
	uart_irq_rx_enable(uart_in);

	printk("sbus: D0/PA10 115200 8N1 -> D13/PB3 S.BUS 100k 8E2\n");

	for (;;) {
		uint32_t rx;
		uint32_t tx;
		uint32_t drops;
		uint32_t err;
		uint32_t tx_delta;
		uint32_t fps;

		k_sleep(K_SECONDS(10));

		rx = (uint32_t)atomic_get(&pipe.rx_bytes);
		tx = (uint32_t)atomic_get(&pipe.tx_bytes);
		drops = (uint32_t)atomic_get(&pipe.rx_drops);
		err = (uint32_t)atomic_get(&pipe.rx_err);
		tx_delta = tx - tx_prev;
		fps = tx_delta / 25U / 10U;
		tx_prev = tx;

		printk("sbus: rx=%u tx=%u drops=%u err=%u fps=%u\n",
		       rx, tx, drops, err, fps);
	}
}
