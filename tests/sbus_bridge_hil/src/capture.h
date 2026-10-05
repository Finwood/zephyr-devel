/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_BRIDGE_HIL_CAPTURE_H_
#define SBUS_BRIDGE_HIL_CAPTURE_H_

#include <stdint.h>

#include <zephyr/kernel.h>

int hil_capture_init(void);
int hil_capture_arm(void);
int hil_capture_wait(uint32_t *t_uart, uint32_t *t_sbus, k_timeout_t timeout);
uint32_t hil_capture_ticks_to_us(uint32_t ticks);
void hil_capture_disarm(void);

#endif
