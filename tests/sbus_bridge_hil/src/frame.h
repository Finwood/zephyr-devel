/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_BRIDGE_HIL_FRAME_H_
#define SBUS_BRIDGE_HIL_FRAME_H_

#include <stdbool.h>
#include <stdint.h>

#define HIL_FRAME_LEN 25
#define HIL_HDR       0x0F
#define HIL_FTR       0x00
#define HIL_FILL      0x5A

void hil_frame_encode(uint8_t out[HIL_FRAME_LEN], uint32_t seq, uint32_t t_us);
bool hil_frame_check(const uint8_t in[HIL_FRAME_LEN], uint32_t *seq, uint32_t *t_us);

#endif
