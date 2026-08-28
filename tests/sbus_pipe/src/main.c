/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "sbus_pipe.h"

static struct sbus_pipe pipe;

static void before(void *f)
{
	ARG_UNUSED(f);
	sbus_pipe_init(&pipe);
}

ZTEST(sbus_pipe, test_empty_pop_fails)
{
	uint8_t b = 0xAA;

	zassert_true(sbus_pipe_is_empty(&pipe));
	zassert_false(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, 0xAA);
	zassert_equal(atomic_get(&pipe.tx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_drops), 0);
}

ZTEST(sbus_pipe, test_push_pop_one)
{
	uint8_t b = 0;

	zassert_true(sbus_pipe_push(&pipe, 0x5A));
	zassert_false(sbus_pipe_is_empty(&pipe));
	zassert_equal(atomic_get(&pipe.rx_bytes), 1);
	zassert_true(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, 0x5A);
	zassert_true(sbus_pipe_is_empty(&pipe));
	zassert_equal(atomic_get(&pipe.tx_bytes), 1);
}

ZTEST(sbus_pipe, test_drop_newest_keeps_queued)
{
	uint8_t b;

	for (int i = 0; i < SBUS_PIPE_CAP; i++) {
		zassert_true(sbus_pipe_push(&pipe, (uint8_t)i));
	}

	zassert_false(sbus_pipe_push(&pipe, 0xFF));
	zassert_equal(atomic_get(&pipe.rx_bytes), SBUS_PIPE_CAP);
	zassert_equal(atomic_get(&pipe.rx_drops), 1);

	for (int i = 0; i < SBUS_PIPE_CAP; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
		zassert_equal(b, (uint8_t)i);
	}
	zassert_false(sbus_pipe_pop(&pipe, &b));
	zassert_equal(atomic_get(&pipe.tx_bytes), SBUS_PIPE_CAP);
}

ZTEST(sbus_pipe, test_wrap_around)
{
	uint8_t b;

	for (int i = 0; i < SBUS_PIPE_CAP; i++) {
		zassert_true(sbus_pipe_push(&pipe, (uint8_t)i));
	}
	for (int i = 0; i < 40; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
		zassert_equal(b, (uint8_t)i);
	}
	for (int i = 0; i < 40; i++) {
		zassert_true(sbus_pipe_push(&pipe, (uint8_t)(100 + i)));
	}
	for (int i = 40; i < SBUS_PIPE_CAP; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
		zassert_equal(b, (uint8_t)i);
	}
	for (int i = 0; i < 40; i++) {
		zassert_true(sbus_pipe_pop(&pipe, &b));
		zassert_equal(b, (uint8_t)(100 + i));
	}
	zassert_true(sbus_pipe_is_empty(&pipe));
	zassert_equal(atomic_get(&pipe.rx_bytes), SBUS_PIPE_CAP + 40);
	zassert_equal(atomic_get(&pipe.tx_bytes), SBUS_PIPE_CAP + 40);
	zassert_equal(atomic_get(&pipe.rx_drops), 0);
}

ZTEST_SUITE(sbus_pipe, NULL, NULL, before, NULL, NULL);
