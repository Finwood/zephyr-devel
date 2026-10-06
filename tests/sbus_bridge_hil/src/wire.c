/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wire.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define UART_OUT_NODE  DT_ALIAS(uart_out)
#define SBUS_IN_NODE   DT_ALIAS(sbus_in)
#define HIL_RX_LOG_MAX 128

BUILD_ASSERT(DT_NODE_EXISTS(UART_OUT_NODE), "alias uart-out missing");
BUILD_ASSERT(DT_NODE_EXISTS(SBUS_IN_NODE), "alias sbus-in missing");
BUILD_ASSERT(DT_IRQ(UART_OUT_NODE, priority) == 1, "uart-out IRQ priority must be 1");
BUILD_ASSERT(DT_IRQ(SBUS_IN_NODE, priority) == 1, "sbus-in IRQ priority must be 1");

struct hil_rx_entry {
	uint32_t seq;
	uint32_t t_us;
};

static const struct device *const uart_out = DEVICE_DT_GET(UART_OUT_NODE);
static const struct device *const sbus_in = DEVICE_DT_GET(SBUS_IN_NODE);
static struct hil_rx_entry rx_log[HIL_RX_LOG_MAX];
static atomic_t rx_count;
static atomic_t corrupt_count;
static uint8_t rx_frame[HIL_FRAME_LEN];
static size_t rx_pos;
static int64_t last_rx_ms = -1;
static uint8_t tx_buf[HIL_FRAME_LEN];
static size_t tx_len;
static size_t tx_pos;
K_SEM_DEFINE(tx_done, 0, 1);
K_MUTEX_DEFINE(tx_lock);

static void uart_out_cb(const struct device *dev, void *user_data)
{
	int sent;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	while (tx_pos < tx_len && uart_irq_tx_ready(dev) > 0) {
		sent = uart_fifo_fill(dev, &tx_buf[tx_pos], tx_len - tx_pos);
		if (sent <= 0) {
			break;
		}
		tx_pos += (size_t)sent;
	}

	if (tx_pos == tx_len) {
		uart_irq_tx_disable(dev);
		k_sem_give(&tx_done);
	}
}

static int hil_wire_send(const uint8_t *data, size_t len)
{
	int ret;

	if (data == NULL || len == 0U || len > sizeof(tx_buf)) {
		return -EINVAL;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	k_sem_reset(&tx_done);
	memcpy(tx_buf, data, len);
	tx_len = len;
	tx_pos = 0U;
	uart_irq_tx_enable(uart_out);
	ret = k_sem_take(&tx_done, K_MSEC(50));
	if (ret != 0) {
		uart_irq_tx_disable(uart_out);
	}
	k_mutex_unlock(&tx_lock);

	return ret;
}

static void rx_byte(uint8_t byte)
{
	uint32_t seq;
	uint32_t t_us;
	atomic_val_t count;

	last_rx_ms = k_uptime_get();

	if (rx_pos == 0U && byte != HIL_HDR) {
		atomic_inc(&corrupt_count);
		return;
	}

	rx_frame[rx_pos++] = byte;
	if (rx_pos < HIL_FRAME_LEN) {
		return;
	}

	rx_pos = 0U;
	if (!hil_frame_check(rx_frame, &seq, &t_us)) {
		atomic_inc(&corrupt_count);
		return;
	}

	count = atomic_get(&rx_count);
	if (count < HIL_RX_LOG_MAX) {
		rx_log[count].seq = seq;
		rx_log[count].t_us = t_us;
		atomic_inc(&rx_count);
	}
}

static void sbus_in_cb(const struct device *dev, void *user_data)
{
	uint8_t bytes[16];
	int err;
	int len;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	err = uart_err_check(dev);
	if ((err & (UART_ERROR_PARITY | UART_ERROR_FRAMING)) != 0) {
		atomic_inc(&corrupt_count);
		rx_pos = 0U;
	}

	while (uart_irq_rx_ready(dev) > 0) {
		len = uart_fifo_read(dev, bytes, sizeof(bytes));
		if (len <= 0) {
			break;
		}
		for (int i = 0; i < len; i++) {
			rx_byte(bytes[i]);
		}
	}
}

int hil_wire_init(void)
{
	int ret;

	if (!device_is_ready(uart_out) || !device_is_ready(sbus_in)) {
		return -ENODEV;
	}

	ret = uart_irq_callback_user_data_set(uart_out, uart_out_cb, NULL);
	if (ret != 0) {
		return ret;
	}
	ret = uart_irq_callback_user_data_set(sbus_in, sbus_in_cb, NULL);
	if (ret != 0) {
		return ret;
	}

	hil_wire_reset();
	uart_irq_err_enable(sbus_in);
	uart_irq_rx_enable(sbus_in);
	return 0;
}

void hil_wire_reset(void)
{
	unsigned int key = irq_lock();

	atomic_clear(&rx_count);
	atomic_clear(&corrupt_count);
	rx_pos = 0U;
	last_rx_ms = -1;

	irq_unlock(key);
}

int hil_wire_send_frame(const uint8_t frame[HIL_FRAME_LEN])
{
	return hil_wire_send(frame, HIL_FRAME_LEN);
}

int hil_wire_send_bytes(const uint8_t *data, size_t len)
{
	if (data == NULL && len > 0U) {
		return -EINVAL;
	}

	while (len > 0U) {
		size_t n = MIN(len, sizeof(tx_buf));
		int ret = hil_wire_send(data, n);

		if (ret != 0) {
			return ret;
		}
		data += n;
		len -= n;
	}

	return 0;
}

int hil_wire_drain(void)
{
	uint8_t pad[HIL_FRAME_LEN - 1U];

	/* 24 footers: finishes any mid-COLLECT window (needs at most 24 bytes
	 * when len==1). Footer 0x00 is valid S.BUS, so the DUT commits/emits
	 * that junk frame and returns to HUNT. While already hunting, 0x00 is
	 * discarded.
	 */
	memset(pad, HIL_FTR, sizeof(pad));
	return hil_wire_send(pad, sizeof(pad));
}

size_t hil_wire_rx_count(void)
{
	return (size_t)atomic_get(&rx_count);
}

bool hil_wire_rx_get(size_t i, uint32_t *seq, uint32_t *t_us)
{
	unsigned int key;

	if (seq == NULL || t_us == NULL) {
		return false;
	}

	key = irq_lock();
	if (i >= (size_t)atomic_get(&rx_count)) {
		irq_unlock(key);
		return false;
	}
	*seq = rx_log[i].seq;
	*t_us = rx_log[i].t_us;
	irq_unlock(key);
	return true;
}

uint32_t hil_wire_corrupt_count(void)
{
	return (uint32_t)atomic_get(&corrupt_count);
}

bool hil_wire_idle_since(int64_t since_ms, int32_t window_ms)
{
	int64_t now = k_uptime_get();
	int64_t last;

	if (window_ms < 0 || now < since_ms + window_ms) {
		return false;
	}

	last = hil_wire_last_rx_ms();
	return last < since_ms;
}

int64_t hil_wire_last_rx_ms(void)
{
	unsigned int key = irq_lock();
	int64_t last = last_rx_ms;

	irq_unlock(key);
	return last;
}
