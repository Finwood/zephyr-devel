/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "capture.h"
#include "frame.h"
#include "wire.h"

#define HIL_STREAM_N               100
#define HIL_PACED_PERIOD_MS        10
#define HIL_PACED_SEND_GAP_MIN_US  8000
#define HIL_PACED_SEND_GAP_MAX_US  12000
#define HIL_RTT_MIN_US             50
#define HIL_RTT_MAX_US             150
#define HIL_GAPLESS_RX_MIN         65
#define HIL_GAPLESS_RX_MAX         85
#define HIL_GAPLESS_SEND_GAP_MIN_US 1000
#define HIL_GAPLESS_SEND_GAP_MAX_US 6000
#define HIL_UART_BYTE_US           87 /* 10/115200 ~= 86.8 */
#define HIL_FRAME_UART_US          (HIL_FRAME_LEN * HIL_UART_BYTE_US)
#define HIL_IDLE_MS                20
#define HIL_SETTLE_MS              5
#define HIL_FIRST_FRAME_TO_MS      50
#define HIL_SETTLE_TIMEOUT_MS      2000
#define HIL_CAPTURE_TIMEOUT_MS     20

static void *eol_setup(void)
{
	zassert_ok(hil_wire_init());
	zassert_ok(hil_capture_init());
	return NULL;
}

static uint32_t now_us(void)
{
	/* 32-bit cycle-to-us wraps in ~25 s; short-stream diffs are modular. */
	return k_cyc_to_us_near32(k_cycle_get_32());
}

/* Wait until no RX byte has been seen for HIL_SETTLE_MS. */
static void settle(void)
{
	int64_t deadline = k_uptime_get() + HIL_SETTLE_TIMEOUT_MS;

	while (k_uptime_get() < deadline) {
		int64_t last = hil_wire_last_rx_ms();

		if (last < 0 || (k_uptime_get() - last) >= HIL_SETTLE_MS) {
			return;
		}
		k_msleep(1);
	}

	zassert_unreachable("DUT output did not settle within %d ms", HIL_SETTLE_TIMEOUT_MS);
}

static void assert_stream(const char *name, uint32_t sent_last, size_t rx_min, size_t rx_max,
			  uint32_t seq_gap_max, uint32_t send_gap_min_us, uint32_t send_gap_max_us,
			  bool check_idle)
{
	uint32_t seq_gap_lo = UINT32_MAX;
	uint32_t seq_gap_hi = 0U;
	uint32_t send_gap_lo = UINT32_MAX;
	uint32_t send_gap_hi = 0U;
	uint32_t prev_seq = 0U;
	uint32_t prev_t_us = 0U;
	uint32_t seq = 0U;
	uint32_t t_us = 0U;
	size_t count;

	/* Let the tail of the DUT output arrive before judging the stream. */
	settle();

	int64_t idle_since = hil_wire_last_rx_ms() + 1;
	size_t rx_at_settle = hil_wire_rx_count();
	uint32_t corrupt_at_settle = hil_wire_corrupt_count();

	zassert_equal(corrupt_at_settle, 0, "%s: corrupt frames/bytes seen", name);

	count = hil_wire_rx_count();
	zassert_true(count >= rx_min && count <= rx_max, "%s: rx_count %zu outside [%zu, %zu]",
		     name, count, rx_min, rx_max);

	for (size_t i = 0; i < count; i++) {
		zassert_true(hil_wire_rx_get(i, &seq, &t_us), "%s: rx_get(%zu) failed", name, i);

		if (i > 0) {
			uint32_t seq_gap = seq - prev_seq;
			uint32_t send_gap = t_us - prev_t_us; /* modular */

			zassert_true(seq > prev_seq && seq_gap >= 1U && seq_gap <= seq_gap_max,
				     "%s: seq gap %u at rx %zu (seq %u -> %u), max %u", name,
				     seq_gap, i, prev_seq, seq, seq_gap_max);
			zassert_true(send_gap >= send_gap_min_us && send_gap <= send_gap_max_us,
				     "%s: send gap %u us at rx %zu (seq %u -> %u), want [%u, %u]",
				     name, send_gap, i, prev_seq, seq, send_gap_min_us,
				     send_gap_max_us);

			seq_gap_lo = MIN(seq_gap_lo, seq_gap);
			seq_gap_hi = MAX(seq_gap_hi, seq_gap);
			send_gap_lo = MIN(send_gap_lo, send_gap);
			send_gap_hi = MAX(send_gap_hi, send_gap);
		}

		prev_seq = seq;
		prev_t_us = t_us;
	}

	zassert_equal(seq, sent_last, "%s: last seq %u != sent %u", name, seq, sent_last);

	TC_PRINT("%s: rx=%zu seq_gap[min=%u max=%u] send_gap_us[min=%u max=%u]\n", name, count,
		 seq_gap_lo, seq_gap_hi, send_gap_lo, send_gap_hi);

	if (check_idle) {
		k_msleep(HIL_IDLE_MS);
		zassert_true(hil_wire_idle_since(idle_since, HIL_IDLE_MS),
			     "%s: unexpected RX after final frame", name);
		zassert_equal(hil_wire_rx_count(), rx_at_settle, "%s: rx_count changed during idle",
			      name);
		zassert_equal(hil_wire_corrupt_count(), corrupt_at_settle,
			      "%s: corrupt count changed during idle", name);
	}
}

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

ZTEST(eol, test_cut_through)
{
	uint8_t frame[HIL_FRAME_LEN];
	uint32_t t_uart;
	uint32_t t_sbus;
	uint32_t seq;
	uint32_t t_us;
	uint32_t rtt_us;

	settle();
	hil_wire_reset();
	zassert_ok(hil_capture_arm());

	hil_frame_encode(frame, 0, now_us());
	/* Armed before send so the UART falling edge is not missed. */
	zassert_ok(hil_wire_send_frame(frame));

	zassert_ok(hil_capture_wait(&t_uart, &t_sbus, K_MSEC(HIL_CAPTURE_TIMEOUT_MS)));
	hil_capture_disarm();

	/* Cut-through: S.BUS start appears before the input frame would have finished. */
	rtt_us = hil_capture_ticks_to_us(t_sbus - t_uart); /* modular 32-bit subtract */
	zassert_true(rtt_us < HIL_FRAME_UART_US, "rtt %u us is not < frame time %u us", rtt_us,
		     HIL_FRAME_UART_US);

	/* The whole valid frame must follow. */
	zassert_true(WAIT_FOR(hil_wire_rx_count() >= 1, 50000, k_msleep(1)),
		     "no complete frame on S.BUS");
	settle();
	zassert_equal(hil_wire_rx_count(), 1);
	zassert_equal(hil_wire_corrupt_count(), 0);
	zassert_true(hil_wire_rx_get(0, &seq, &t_us));
	zassert_equal(seq, 0);

	TC_PRINT("cut-through rtt=%u us\n", rtt_us);
}

ZTEST(eol, test_paced)
{
	uint8_t frame[HIL_FRAME_LEN];
	uint32_t rtt_min_us = UINT32_MAX;
	uint32_t rtt_max_us = 0U;
	int64_t start_ms;

	settle();
	hil_wire_reset();

	start_ms = k_uptime_get();
	for (uint32_t i = 0; i < HIL_STREAM_N; i++) {
		uint32_t t_uart;
		uint32_t t_sbus;
		uint32_t rtt_us;

		zassert_ok(hil_capture_arm());
		hil_frame_encode(frame, i, now_us());
		zassert_ok(hil_wire_send_frame(frame));
		zassert_ok(hil_capture_wait(&t_uart, &t_sbus, K_MSEC(HIL_CAPTURE_TIMEOUT_MS)),
			   "frame %u: no capture", i);
		hil_capture_disarm();

		rtt_us = hil_capture_ticks_to_us(t_sbus - t_uart);
		zassert_true(rtt_us >= HIL_RTT_MIN_US && rtt_us <= HIL_RTT_MAX_US,
			     "frame %u: rtt %u us outside [%u, %u]", i, rtt_us, HIL_RTT_MIN_US,
			     HIL_RTT_MAX_US);
		rtt_min_us = MIN(rtt_min_us, rtt_us);
		rtt_max_us = MAX(rtt_max_us, rtt_us);

		if (i == 0U) {
			/* First valid frame must be out within HIL_FIRST_FRAME_TO_MS of send. */
			zassert_true(WAIT_FOR(hil_wire_rx_count() >= 1,
					      (HIL_FIRST_FRAME_TO_MS -
					       (k_uptime_get() - start_ms)) * USEC_PER_MSEC,
					      k_msleep(1)),
				     "no valid frame within %d ms of first send",
				     HIL_FIRST_FRAME_TO_MS);
		}

		k_sleep(K_TIMEOUT_ABS_MS(start_ms + (int64_t)(i + 1U) * HIL_PACED_PERIOD_MS));
	}

	TC_PRINT("paced: rtt_us[min=%u max=%u]\n", rtt_min_us, rtt_max_us);

	assert_stream("paced", HIL_STREAM_N - 1, HIL_STREAM_N, HIL_STREAM_N, 1,
		      HIL_PACED_SEND_GAP_MIN_US, HIL_PACED_SEND_GAP_MAX_US, true);
}

ZTEST(eol, test_gapless)
{
	uint8_t frame[HIL_FRAME_LEN];

	settle();
	hil_wire_reset();
	hil_capture_disarm();

	for (uint32_t i = 0; i < HIL_STREAM_N; i++) {
		hil_frame_encode(frame, i, now_us());
		zassert_ok(hil_wire_send_frame(frame), "frame %u: send failed", i);
	}

	assert_stream("gapless", HIL_STREAM_N - 1, HIL_GAPLESS_RX_MIN, HIL_GAPLESS_RX_MAX, 2,
		      HIL_GAPLESS_SEND_GAP_MIN_US, HIL_GAPLESS_SEND_GAP_MAX_US, true);
}

ZTEST_SUITE(eol, NULL, eol_setup, NULL, NULL, NULL);

static int cmd_eol(const struct shell *sh, size_t argc, char **argv)
{
	int fail;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	fail = ztest_run_test_suite(eol, false, 1, 1, NULL);
	if (fail == 0) {
		shell_print(sh, "eol: PASS");
		return 0;
	}
	shell_print(sh, "eol: FAIL");
	return -EIO;
}

SHELL_CMD_REGISTER(eol, NULL, "Re-run sbus_bridge HIL suite", cmd_eol);
