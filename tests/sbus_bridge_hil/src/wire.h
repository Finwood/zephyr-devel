/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_BRIDGE_HIL_WIRE_H_
#define SBUS_BRIDGE_HIL_WIRE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"

int hil_wire_init(void);
void hil_wire_reset(void);
int hil_wire_send_frame(const uint8_t frame[HIL_FRAME_LEN]);
size_t hil_wire_rx_count(void);
bool hil_wire_rx_get(size_t i, uint32_t *seq, uint32_t *t_us);
uint32_t hil_wire_corrupt_count(void);

/*
 * True when no RX byte occurred in [since_ms, since_ms + window_ms].
 *
 * Call only after the window has ended (k_uptime_get() >= since_ms + window_ms);
 * before that the function always returns false and does not sleep. After the
 * window, returns true iff hil_wire_last_rx_ms() < since_ms (any byte at or
 * after since_ms means the interval was not idle).
 */
bool hil_wire_idle_since(int64_t since_ms, int32_t window_ms);
int64_t hil_wire_last_rx_ms(void);

#endif
