/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_PIPE_H_
#define SBUS_PIPE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/spsc_lockfree.h>

#define SBUS_PIPE_CAP 64

SPSC_DECLARE(byte, uint8_t);

struct sbus_pipe {
	struct spsc_byte q;
	uint8_t buf[SBUS_PIPE_CAP];
	atomic_t rx_bytes;
	atomic_t tx_bytes;
	atomic_t rx_drops;
	atomic_t rx_err;
};

void sbus_pipe_init(struct sbus_pipe *p);
bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b);
bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b);
bool sbus_pipe_is_empty(const struct sbus_pipe *p);

#endif /* SBUS_PIPE_H_ */
