/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "sbus_pipe.h"

static struct sbus_pipe pipe;

static void before(void *f)
{
	ARG_UNUSED(f);
	sbus_pipe_init(&pipe);
}

static void fill_frame(uint8_t frame[SBUS_FRAME_LEN], uint8_t fill, uint8_t footer)
{
	frame[0] = SBUS_HDR;
	for (int i = 1; i < SBUS_FRAME_LEN - 1; i++) {
		frame[i] = fill;
	}
	frame[SBUS_FRAME_LEN - 1] = footer;
}

static void push_bytes(const uint8_t *b, int n)
{
	for (int i = 0; i < n; i++) {
		sbus_pipe_push(&pipe, b[i]);
	}
}

static void push_frame(uint8_t fill, uint8_t footer)
{
	uint8_t frame[SBUS_FRAME_LEN];

	fill_frame(frame, fill, footer);
	push_bytes(frame, SBUS_FRAME_LEN);
}

static void drain_n(int n)
{
	uint8_t b;

	for (int i = 0; i < n; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b), "pop %d failed", i);
	}
}

ZTEST(sbus_pipe, test_init_three_distinct_slots)
{
	zassert_equal(pipe.current, &pipe.slots[0]);
	zassert_equal(pipe.next, &pipe.slots[1]);
	zassert_equal(pipe.scratch, &pipe.slots[2]);
	zassert_is_null(pipe.collect);
	zassert_equal(pipe.current->len, 0);
	zassert_equal(pipe.next->len, 0);
	zassert_equal(pipe.scratch->len, 0);
}

ZTEST(sbus_pipe, test_empty_pop_fails)
{
	uint8_t b = 0xAA;

	zassert_true(sbus_pipe_is_empty(&pipe));
	zassert_false(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, 0xAA);
	zassert_equal(atomic_get(&pipe.tx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_frames), 0);
	zassert_is_null(pipe.collect);
}

ZTEST(sbus_pipe, test_hunt_ignores_noise)
{
	zassert_false(sbus_pipe_push(&pipe, 0x00));
	zassert_false(sbus_pipe_push(&pipe, 0xAA));
	zassert_false(sbus_pipe_push(&pipe, 0x0E));
	zassert_equal(atomic_get(&pipe.sync_err), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 0);
	zassert_is_null(pipe.collect);

	zassert_true(sbus_pipe_push(&pipe, SBUS_HDR));
	zassert_equal(pipe.collect, pipe.current);
	zassert_equal(pipe.current->len, 1);
	zassert_equal(pipe.current->buf[0], SBUS_HDR);
	zassert_equal(atomic_get(&pipe.sync_err), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 1);
}

ZTEST(sbus_pipe, test_valid_0x00_and_interior_0x0f)
{
	uint8_t frame[SBUS_FRAME_LEN];
	uint8_t b;

	fill_frame(frame, 0x11, SBUS_FTR);
	frame[5] = SBUS_HDR;
	push_bytes(frame, SBUS_FRAME_LEN);

	zassert_equal(atomic_get(&pipe.rx_frames), 1);
	zassert_equal(atomic_get(&pipe.sync_err), 0);
	zassert_true(pipe.current->complete);
	zassert_is_null(pipe.collect);
	zassert_equal(atomic_get(&pipe.rx_bytes), SBUS_FRAME_LEN);

	for (int i = 0; i < SBUS_FRAME_LEN; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
		zassert_equal(b, frame[i]);
	}
	zassert_equal(atomic_get(&pipe.tx_frames), 1);
	zassert_true(sbus_pipe_is_empty(&pipe));
}

ZTEST(sbus_pipe, test_footer_0x01_always_sync_err)
{
	push_frame(0x22, 0x01);

	zassert_equal(atomic_get(&pipe.sync_err), 1);
	zassert_equal(atomic_get(&pipe.rx_frames), 0);
	zassert_false(pipe.current->complete);
	zassert_is_null(pipe.collect);
}

ZTEST(sbus_pipe, test_footer_0x04_and_0x14)
{
	push_frame(0x33, 0x04);
	if (IS_ENABLED(CONFIG_UART_SBUS_SBUS2)) {
		zassert_equal(atomic_get(&pipe.rx_frames), 1);
		zassert_equal(atomic_get(&pipe.sync_err), 0);
		zassert_true(pipe.current->complete);
	} else {
		zassert_equal(atomic_get(&pipe.rx_frames), 0);
		zassert_equal(atomic_get(&pipe.sync_err), 1);
		zassert_false(pipe.current->complete);
	}

	sbus_pipe_init(&pipe);
	push_frame(0x33, 0x14);
	if (IS_ENABLED(CONFIG_UART_SBUS_SBUS2)) {
		zassert_equal(atomic_get(&pipe.rx_frames), 1);
		zassert_equal(atomic_get(&pipe.sync_err), 0);
		zassert_true(pipe.current->complete);
	} else {
		zassert_equal(atomic_get(&pipe.rx_frames), 0);
		zassert_equal(atomic_get(&pipe.sync_err), 1);
	}
}

ZTEST(sbus_pipe, test_bad_footer_cut_through_drains_and_promotes)
{
	uint8_t b;
	struct sbus_slot *orig_current = pipe.current;
	struct sbus_slot *orig_next = pipe.next;

	push_frame(0x44, 0x01);
	zassert_equal(atomic_get(&pipe.sync_err), 1);
	zassert_equal(orig_current->len, SBUS_FRAME_LEN);
	zassert_false(orig_current->complete);

	for (int i = 0; i < SBUS_FRAME_LEN; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
	}
	zassert_equal(atomic_get(&pipe.tx_frames), 0);
	zassert_equal(pipe.current, orig_next);
	zassert_equal(pipe.next, orig_current);
	zassert_equal(pipe.next->len, 0);
	zassert_true(sbus_pipe_is_empty(&pipe));
}

ZTEST(sbus_pipe, test_bad_footer_next_not_complete)
{
	push_frame(0x55, SBUS_FTR);
	drain_n(1);
	zassert_true(pipe.current->tx_started);

	push_frame(0x66, 0x01);
	zassert_equal(atomic_get(&pipe.sync_err), 1);
	zassert_equal(atomic_get(&pipe.rx_frames), 1);
	zassert_false(pipe.next->complete);
	zassert_equal(pipe.next->len, 0);
	zassert_equal(pipe.current->buf[1], 0x55);
}

ZTEST(sbus_pipe, test_cut_through_len_grows_before_footer)
{
	zassert_true(sbus_pipe_push(&pipe, SBUS_HDR));
	zassert_true(sbus_pipe_push(&pipe, 0x10));
	zassert_true(sbus_pipe_push(&pipe, 0x20));
	zassert_equal(pipe.current->len, 3);
	zassert_false(pipe.current->complete);
	zassert_equal(pipe.current->buf[0], SBUS_HDR);
	zassert_equal(pipe.current->buf[1], 0x10);
	zassert_equal(pipe.current->buf[2], 0x20);
	zassert_equal(atomic_get(&pipe.rx_frames), 0);
	zassert_false(sbus_pipe_is_empty(&pipe));
}

ZTEST(sbus_pipe, test_in_flight_fills_next)
{
	uint8_t saved;

	push_frame(0x77, SBUS_FTR);
	saved = pipe.current->buf[3];
	drain_n(1);
	zassert_true(pipe.current->tx_started);

	zassert_true(sbus_pipe_push(&pipe, SBUS_HDR));
	zassert_true(sbus_pipe_push(&pipe, 0x88));
	zassert_equal(pipe.current->buf[3], saved);
	zassert_equal(pipe.current->len, SBUS_FRAME_LEN);
	zassert_equal(pipe.collect, pipe.next);
	zassert_equal(pipe.collect->len, 2);
	zassert_equal(pipe.next->buf[0], SBUS_HDR);
	zassert_equal(pipe.next->buf[1], 0x88);
	zassert_equal(pipe.scratch->len, 0);
	zassert_false(pipe.next->complete);
}

ZTEST(sbus_pipe, test_supersede_keeps_newest_next)
{
	struct sbus_slot *waiting;
	struct sbus_slot *staging;

	push_frame(0xA1, SBUS_FTR);
	drain_n(1);

	push_frame(0xB2, SBUS_FTR);
	zassert_equal(atomic_get(&pipe.supersede), 0);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xB2);

	waiting = pipe.next;
	staging = pipe.scratch;
	push_frame(0xC3, SBUS_FTR);
	zassert_equal(atomic_get(&pipe.supersede), 1);
	zassert_equal(pipe.next, staging);
	zassert_equal(pipe.scratch, waiting);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xC3);
	zassert_equal(pipe.scratch->len, 0);
	zassert_equal(pipe.current->buf[1], 0xA1);
	zassert_equal(atomic_get(&pipe.rx_frames), 3);
}

ZTEST(sbus_pipe, test_bad_footer_preserves_complete_next)
{
	push_frame(0xA1, SBUS_FTR);
	drain_n(1);
	push_frame(0xB2, SBUS_FTR);

	push_frame(0xC3, 0x01);

	zassert_equal(atomic_get(&pipe.sync_err), 1);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xB2);
	zassert_equal(pipe.scratch->len, 0);
	zassert_false(pipe.scratch->complete);
	zassert_equal(atomic_get(&pipe.supersede), 0);
}

ZTEST(sbus_pipe, test_promote_mid_collect_empty_next)
{
	uint8_t prefix[] = {SBUS_HDR, 0xB2, 0xB3};
	uint8_t remainder[SBUS_FRAME_LEN - ARRAY_SIZE(prefix)];
	uint8_t b;

	push_frame(0xA1, SBUS_FTR);
	drain_n(1);
	push_bytes(prefix, ARRAY_SIZE(prefix));
	drain_n(SBUS_FRAME_LEN - 1);

	zassert_equal(pipe.collect, pipe.current);
	zassert_equal(pipe.collect->len, ARRAY_SIZE(prefix));
	zassert_equal(pipe.current->len, ARRAY_SIZE(prefix));
	zassert_equal(pipe.current->buf[0], SBUS_HDR);
	zassert_equal(pipe.current->buf[1], 0xB2);
	zassert_equal(pipe.current->buf[2], 0xB3);
	zassert_false(pipe.current->complete);

	memset(remainder, 0xB2, sizeof(remainder));
	remainder[ARRAY_SIZE(remainder) - 1] = SBUS_FTR;
	push_bytes(remainder, ARRAY_SIZE(remainder));

	zassert_equal(atomic_get(&pipe.rx_frames), 2);
	zassert_true(pipe.current->complete);
	zassert_equal(pipe.current->len, SBUS_FRAME_LEN);
	zassert_true(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, SBUS_HDR);
	zassert_true(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, 0xB2);
}

ZTEST(sbus_pipe, test_promote_mid_collect_complete_next)
{
	uint8_t prefix[] = {SBUS_HDR, 0xC3};
	uint8_t remainder[SBUS_FRAME_LEN - ARRAY_SIZE(prefix)];
	struct sbus_slot *staging;
	struct sbus_slot *idle_next;

	push_frame(0xA1, SBUS_FTR);
	drain_n(1);
	push_frame(0xB2, SBUS_FTR);
	push_bytes(prefix, ARRAY_SIZE(prefix));
	zassert_equal(pipe.collect, pipe.scratch);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xB2);
	drain_n(SBUS_FRAME_LEN - 1);

	zassert_true(pipe.current->complete);
	zassert_equal(pipe.current->buf[1], 0xB2);
	zassert_equal(pipe.collect, pipe.scratch);
	zassert_equal(pipe.collect->len, ARRAY_SIZE(prefix));
	zassert_equal(pipe.scratch->buf[0], SBUS_HDR);
	zassert_equal(pipe.scratch->buf[1], 0xC3);
	zassert_equal(pipe.next->len, 0);
	zassert_false(pipe.next->complete);

	staging = pipe.scratch;
	idle_next = pipe.next;
	memset(remainder, 0xC3, sizeof(remainder));
	remainder[ARRAY_SIZE(remainder) - 1] = SBUS_FTR;
	push_bytes(remainder, ARRAY_SIZE(remainder));

	zassert_equal(pipe.next, staging);
	zassert_equal(pipe.scratch, idle_next);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xC3);
	zassert_equal(pipe.scratch->len, 0);
	zassert_equal(pipe.current->buf[1], 0xB2);
	zassert_equal(atomic_get(&pipe.supersede), 0);
}

ZTEST(sbus_pipe, test_promote_mid_collect_complete_next_bad_footer)
{
	uint8_t prefix[] = {SBUS_HDR, 0xC3};
	uint8_t remainder[SBUS_FRAME_LEN - ARRAY_SIZE(prefix)];

	push_frame(0xA1, SBUS_FTR);
	drain_n(1);
	push_frame(0xB2, SBUS_FTR);
	push_bytes(prefix, ARRAY_SIZE(prefix));
	drain_n(SBUS_FRAME_LEN - 1);

	zassert_true(pipe.current->complete);
	zassert_equal(pipe.current->buf[1], 0xB2);
	zassert_equal(pipe.collect, pipe.scratch);
	zassert_equal(pipe.collect->len, ARRAY_SIZE(prefix));
	zassert_equal(pipe.next->len, 0);
	zassert_false(pipe.next->complete);

	memset(remainder, 0xC3, sizeof(remainder));
	remainder[ARRAY_SIZE(remainder) - 1] = 0x01;
	push_bytes(remainder, ARRAY_SIZE(remainder));

	zassert_equal(atomic_get(&pipe.sync_err), 1);
	zassert_equal(pipe.next->len, 0);
	zassert_false(pipe.next->complete);
	zassert_equal(pipe.scratch->len, 0);
	zassert_false(pipe.scratch->complete);
	zassert_equal(pipe.current->buf[1], 0xB2);
	zassert_equal(atomic_get(&pipe.rx_frames), 2);
}

ZTEST(sbus_pipe, test_promote_swaps_pointers)
{
	struct sbus_slot *waiting;

	push_frame(0xA1, SBUS_FTR);
	drain_n(1);
	push_frame(0xC3, SBUS_FTR);
	waiting = pipe.next;

	drain_n(SBUS_FRAME_LEN - 1);
	zassert_equal(atomic_get(&pipe.tx_frames), 1);
	zassert_equal(pipe.current, waiting);
	zassert_equal(pipe.current->buf[1], 0xC3);
	zassert_true(pipe.current->complete);
}

ZTEST(sbus_pipe, test_tx_frames_once_per_complete)
{
	uint8_t b;

	push_frame(0x99, SBUS_FTR);
	drain_n(SBUS_FRAME_LEN - 1);
	zassert_equal(atomic_get(&pipe.tx_frames), 0);
	zassert_true(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, SBUS_FTR);
	zassert_equal(atomic_get(&pipe.tx_frames), 1);
	zassert_equal(atomic_get(&pipe.tx_bytes), SBUS_FRAME_LEN);
}

ZTEST_SUITE(sbus_pipe, NULL, NULL, before, NULL, NULL);
