/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sbus_pipe.h"

static void sbus_slot_reset(struct sbus_slot *s)
{
	s->len = 0;
	s->rd = 0;
	s->complete = false;
	s->tx_started = false;
}

static bool sbus_slot_owes_tx(const struct sbus_slot *s)
{
	return s->complete || s->tx_started || (s->rd < s->len);
}

static bool sbus_footer_ok(uint8_t footer)
{
	return (footer & SBUS_FOOTER_MASK) == SBUS_FTR;
}

static bool sbus_collecting_current(const struct sbus_pipe *p)
{
	return p->collect == p->current;
}

static bool sbus_current_drained(const struct sbus_pipe *p)
{
	if (sbus_collecting_current(p)) {
		return false;
	}
	if (p->current->len == 0 && !p->current->tx_started && !p->current->complete) {
		return false;
	}
	return p->current->rd == p->current->len;
}

static void sbus_swap_slots(struct sbus_slot **a, struct sbus_slot **b)
{
	struct sbus_slot *tmp = *a;

	*a = *b;
	*b = tmp;
}

static void sbus_commit_scratch(struct sbus_pipe *p)
{
	if (p->next->complete) {
		atomic_inc(&p->supersede);
	}
	sbus_swap_slots(&p->next, &p->scratch);
	sbus_slot_reset(p->scratch);
}

static void sbus_promote(struct sbus_pipe *p)
{
	sbus_swap_slots(&p->current, &p->next);
	sbus_slot_reset(p->next);
}

void sbus_pipe_init(struct sbus_pipe *p)
{
	sbus_slot_reset(&p->slots[0]);
	sbus_slot_reset(&p->slots[1]);
	sbus_slot_reset(&p->slots[2]);
	p->current = &p->slots[0];
	p->next = &p->slots[1];
	p->scratch = &p->slots[2];
	p->collect = NULL;
	atomic_set(&p->rx_bytes, 0);
	atomic_set(&p->tx_bytes, 0);
	atomic_set(&p->rx_err, 0);
	atomic_set(&p->rx_frames, 0);
	atomic_set(&p->tx_frames, 0);
	atomic_set(&p->supersede, 0);
	atomic_set(&p->sync_err, 0);
}

bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b)
{
	if (p->collect == NULL) {
		if (b != SBUS_HDR) {
			return false;
		}
		if (!sbus_slot_owes_tx(p->current)) {
			p->collect = p->current;
		} else if (!p->next->complete) {
			p->collect = p->next;
		} else {
			p->collect = p->scratch;
		}
	}

	p->collect->buf[p->collect->len] = b;
	p->collect->len++;
	atomic_inc(&p->rx_bytes);

	if (p->collect->len < SBUS_FRAME_LEN) {
		return true;
	}

	if (sbus_footer_ok(p->collect->buf[SBUS_FRAME_LEN - 1])) {
		p->collect->complete = true;
		if (p->collect == p->scratch) {
			sbus_commit_scratch(p);
		}
		atomic_inc(&p->rx_frames);
	} else {
		atomic_inc(&p->sync_err);
		if (p->collect != p->current) {
			sbus_slot_reset(p->collect);
		}
	}

	p->collect = NULL;
	return true;
}

bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b)
{
	if (p->current->rd >= p->current->len) {
		return false;
	}

	*b = p->current->buf[p->current->rd];
	p->current->rd++;
	p->current->tx_started = true;
	atomic_inc(&p->tx_bytes);

	if (sbus_current_drained(p)) {
		if (p->current->complete) {
			atomic_inc(&p->tx_frames);
		}
		sbus_promote(p);
	}

	return true;
}

bool sbus_pipe_is_empty(const struct sbus_pipe *p)
{
	return p->current->rd == p->current->len;
}
