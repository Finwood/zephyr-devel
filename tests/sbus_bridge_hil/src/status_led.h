/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_BRIDGE_HIL_STATUS_LED_H_
#define SBUS_BRIDGE_HIL_STATUS_LED_H_

int hil_status_leds_init(void);
void hil_status_leds_off(void);
void hil_status_leds_pass(void);
void hil_status_leds_fail(void);

#endif
