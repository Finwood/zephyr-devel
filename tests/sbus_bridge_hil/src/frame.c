/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "frame.h"

#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

void hil_frame_encode(uint8_t out[HIL_FRAME_LEN], uint32_t seq, uint32_t t_us)
{
	uint16_t crc;

	out[0] = HIL_HDR;
	sys_put_le32(seq, &out[1]);
	sys_put_le32(t_us, &out[5]);
	memset(&out[9], HIL_FILL, 12);
	crc = crc16_itu_t(0xFFFF, &out[1], 20);
	sys_put_le16(crc, &out[21]);
	out[23] = 0x00;
	out[24] = HIL_FTR;
}

bool hil_frame_check(const uint8_t in[HIL_FRAME_LEN], uint32_t *seq, uint32_t *t_us)
{
	uint16_t expect;

	if (in[0] != HIL_HDR || in[23] != 0x00 || in[24] != HIL_FTR) {
		return false;
	}
	expect = crc16_itu_t(0xFFFF, &in[1], 20);
	if (sys_get_le16(&in[21]) != expect) {
		return false;
	}
	*seq = sys_get_le32(&in[1]);
	*t_us = sys_get_le32(&in[5]);
	return true;
}
