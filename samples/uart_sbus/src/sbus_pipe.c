/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "sbus_pipe.h"

void sbus_pipe_init(struct sbus_pipe *p)
{
	const struct spsc_byte init = SPSC_INITIALIZER(SBUS_PIPE_CAP, p->buf);

	memcpy(&p->q, &init, sizeof(p->q));
	atomic_set(&p->rx_bytes, 0);
	atomic_set(&p->tx_bytes, 0);
	atomic_set(&p->rx_drops, 0);
	atomic_set(&p->rx_err, 0);
}

bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b)
{
	uint8_t *slot = spsc_acquire(&p->q);

	if (slot == NULL) {
		atomic_inc(&p->rx_drops);
		return false;
	}

	*slot = b;
	spsc_produce(&p->q);
	atomic_inc(&p->rx_bytes);
	return true;
}

bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b)
{
	uint8_t *slot = spsc_consume(&p->q);

	if (slot == NULL) {
		return false;
	}

	*b = *slot;
	spsc_release(&p->q);
	atomic_inc(&p->tx_bytes);
	return true;
}

bool sbus_pipe_is_empty(const struct sbus_pipe *p)
{
	struct spsc_byte *q = (struct spsc_byte *)&p->q;

	return spsc_peek(q) == NULL;
}
