/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include "frame.h"

ZTEST(eol, test_frame_crc_vector)
{
	uint8_t frame[HIL_FRAME_LEN];
	uint32_t seq;
	uint32_t t_us;
	static const uint8_t expect[HIL_FRAME_LEN] = {
		0x0f, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
		0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
		0x5a, 0x5a, 0x5a, 0x04, 0x2b, 0x00, 0x00,
	};

	hil_frame_encode(frame, 0, 0x01020304);
	zassert_mem_equal(frame, expect, HIL_FRAME_LEN);
	zassert_true(hil_frame_check(frame, &seq, &t_us));
	zassert_equal(seq, 0);
	zassert_equal(t_us, 0x01020304);
	frame[21] ^= 0x01;
	zassert_false(hil_frame_check(frame, &seq, &t_us));
}

ZTEST_SUITE(eol, NULL, NULL, NULL, NULL, NULL);
