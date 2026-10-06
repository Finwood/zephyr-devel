/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "capture.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define UART_CAPTURE_CHANNEL  0U
#define SBUS_CAPTURE_CHANNEL  1U
#define CAPTURE_COMPLETE_MASK (BIT(UART_CAPTURE_CHANNEL) | BIT(SBUS_CAPTURE_CHANNEL))

static const struct device *const capture_dev = DEVICE_DT_GET(DT_NODELABEL(counter2));
static uint32_t uart_ticks;
static uint32_t sbus_ticks;
static atomic_t capture_mask;
K_SEM_DEFINE(capture_done, 0, 1);

/*
 * The STM32 Zephyr counter driver does not expose CCxOF here, so overcapture
 * cannot fail a case; a missed edge still times out from hil_capture_wait()
 * with -EAGAIN.
 */
static void capture_cb(const struct device *dev, uint8_t channel,
		       counter_capture_flags_t flags, uint32_t ticks, void *user_data)
{
	atomic_val_t previous;

	ARG_UNUSED(dev);
	ARG_UNUSED(flags);
	ARG_UNUSED(user_data);

	if (channel != UART_CAPTURE_CHANNEL && channel != SBUS_CAPTURE_CHANNEL) {
		return;
	}

	if (channel == UART_CAPTURE_CHANNEL) {
		uart_ticks = ticks;
	} else {
		sbus_ticks = ticks;
	}

	previous = atomic_or(&capture_mask, BIT(channel));
	if ((previous | BIT(channel)) == CAPTURE_COMPLETE_MASK) {
		k_sem_give(&capture_done);
	}
}

static int capture_configure(void)
{
	int ret;

	ret = counter_capture_configure(capture_dev, UART_CAPTURE_CHANNEL,
					COUNTER_CAPTURE_FALLING_EDGE | COUNTER_CAPTURE_SINGLE_SHOT,
					capture_cb, NULL);
	if (ret != 0) {
		return ret;
	}

	ret = counter_capture_configure(capture_dev, SBUS_CAPTURE_CHANNEL,
					COUNTER_CAPTURE_RISING_EDGE | COUNTER_CAPTURE_SINGLE_SHOT,
					capture_cb, NULL);
	if (ret != 0) {
		(void)counter_disable_capture(capture_dev, UART_CAPTURE_CHANNEL);
	}

	return ret;
}

int hil_capture_init(void)
{
	int ret;

	if (!device_is_ready(capture_dev)) {
		return -ENODEV;
	}

	ret = capture_configure();
	if (ret != 0) {
		return ret;
	}

	ret = counter_start(capture_dev);
	if (ret != 0) {
		hil_capture_disarm();
	}

	return ret;
}

int hil_capture_arm(void)
{
	int ret;

	hil_capture_disarm();

	ret = capture_configure();
	if (ret != 0) {
		return ret;
	}

	k_sem_reset(&capture_done);
	atomic_clear(&capture_mask);
	uart_ticks = 0U;
	sbus_ticks = 0U;

	ret = counter_enable_capture(capture_dev, UART_CAPTURE_CHANNEL);
	if (ret != 0) {
		hil_capture_disarm();
		return ret;
	}

	ret = counter_enable_capture(capture_dev, SBUS_CAPTURE_CHANNEL);
	if (ret != 0) {
		hil_capture_disarm();
	}

	return ret;
}

int hil_capture_wait(uint32_t *t_uart, uint32_t *t_sbus, k_timeout_t timeout)
{
	int ret;

	if (t_uart == NULL || t_sbus == NULL) {
		return -EINVAL;
	}

	ret = k_sem_take(&capture_done, timeout);
	if (ret != 0) {
		return -EAGAIN;
	}

	if (atomic_get(&capture_mask) != CAPTURE_COMPLETE_MASK) {
		return -EIO;
	}

	*t_uart = uart_ticks;
	*t_sbus = sbus_ticks;
	return 0;
}

uint32_t hil_capture_ticks_to_us(uint32_t ticks)
{
	return (uint32_t)counter_ticks_to_us(capture_dev, ticks);
}

void hil_capture_disarm(void)
{
	(void)counter_disable_capture(capture_dev, UART_CAPTURE_CHANNEL);
	(void)counter_disable_capture(capture_dev, SBUS_CAPTURE_CHANNEL);
}
