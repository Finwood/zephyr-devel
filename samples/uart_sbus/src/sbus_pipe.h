/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBUS_PIPE_H_
#define SBUS_PIPE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define SBUS_FRAME_LEN 25
#define SBUS_HDR       0x0F
#define SBUS_FTR       0x00
/* Off: 0xFF (exact 0x00). On: 0xCB, ignore Futaba slot bits 2,4,5. */
#define SBUS_FOOTER_MASK (IS_ENABLED(CONFIG_UART_SBUS_SBUS2) ? 0xCBu : 0xFFu)

struct sbus_slot {
	uint8_t buf[SBUS_FRAME_LEN];
	uint8_t len;       /* bytes valid (RX write index / TX owed) */
	uint8_t rd;        /* TX read index */
	bool complete;     /* len==25 and footer passed the mask */
	bool tx_started;   /* at least one byte given to USART FIFO */
};

struct sbus_pipe {
	struct sbus_slot slots[3];
	struct sbus_slot *current; /* TX drains this */
	struct sbus_slot *next;    /* successor: partial collect, complete, or empty */
	struct sbus_slot *scratch; /* COLLECT dest when next is already complete */
	struct sbus_slot *collect; /* COLLECT dest; NULL while hunting */

	atomic_t rx_bytes;
	atomic_t tx_bytes;
	atomic_t rx_err;
	atomic_t rx_frames;
	atomic_t tx_frames;
	atomic_t supersede;
	atomic_t sync_err;
};

/**
 * @brief Initialize an S.BUS three-slot assembler.
 *
 * Resets all three frame slots, points current/next/scratch at slots
 * 0/1/2, enters hunt, and zeroes all statistics counters.
 *
 * @param p Pipe instance.
 */
void sbus_pipe_init(struct sbus_pipe *p);

/**
 * @brief Feed one UART RX byte into the assembler.
 *
 * While hunting, non-header bytes are discarded and do not increment
 * @c sync_err. A header (@c SBUS_HDR) starts a locked 25-byte collect
 * window; interior @c 0x0F is payload, not a resync.
 *
 * Bytes are cut through into current when that slot owes no TX.
 * Otherwise they assemble into next when next is not complete, or into
 * scratch when next already holds a complete waiting frame. A valid
 * footer commits the frame (@c rx_frames). A completed scratch is
 * pointer-swapped with next; if next was already complete, the waiting
 * frame is dropped (@c supersede). A bad footer increments @c sync_err;
 * in-flight cut-through bytes on current are left for TX, while an
 * incomplete next or scratch is reset.
 *
 * Increments @c rx_bytes for every accepted collect byte, including the
 * header.
 *
 * @param p Pipe instance.
 * @param b Received byte.
 *
 * @retval true  Byte was accepted into a collect window.
 * @retval false Byte was ignored while hunting.
 */
bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b);

/**
 * @brief Pop the next byte owed to S.BUS TX.
 *
 * Copies the next unread byte from current and advances the read index.
 * The first successful pop on a slot sets @c tx_started. When current
 * is drained (read has caught write, and RX is not still collecting
 * into it), @c tx_frames is incremented if the slot was complete, then
 * the pipe promotes next to current.
 *
 * An idle empty slot is not promoted. A drained incomplete cut-through
 * (bad footer) is promoted so the pipe cannot stall.
 *
 * @param p Pipe instance.
 * @param[out] b Destination for the popped byte. Written only on success.
 *
 * @retval true  A byte was written to @p b.
 * @retval false Nothing is owed to TX on current.
 */
bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b);

/**
 * @brief Check whether TX has caught up on the current slot.
 *
 * True when current's read index equals its length. During cut-through,
 * that is also true between RX bytes (TX's view of a gap).
 *
 * @param p Pipe instance.
 *
 * @retval true  No bytes remain to pop from current.
 * @retval false At least one byte is still owed to TX.
 */
bool sbus_pipe_is_empty(const struct sbus_pipe *p);

#endif /* SBUS_PIPE_H_ */
