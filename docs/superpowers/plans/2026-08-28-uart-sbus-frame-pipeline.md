# UART to S.BUS Frame Pipeline and LEDs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evolve the landed `samples/uart_sbus` cut-through byte pipe into a two-slot classic S.BUS frame assembler with stale-frame drop, green activity LED, and decaying red error LED on `nucleo_g431kb`.

**Architecture:** RX ISR hunts `0x0F` and collects a locked 25-byte window into `current` (cut-through) or `next` (while TX is in-flight). A complete waiting `next` is replaced as a whole on the next valid commit (`supersede`). TX ISR pops `current` and promotes with `irq_lock` pointer swap. Main thread (5 ms `k_sem` timeout) owns GPIO/LED policy and 10 s stats. No `spsc_lockfree` byte ring.

**Tech Stack:** Zephyr interrupt UART API, `irq_lock` / `k_sem`, GPIO `gpio-leds` (`led0` + `sbus-err-led`), module `CONFIG_UART_SBUS_SBUS2`, ztest on `native_sim` (two twister cases), `uv run west` / twister.

**Spec:** `docs/superpowers/specs/2026-08-28-uart-sbus-frame-pipeline-design.md`  
**Supersedes plan (V1 byte-pipe):** `docs/superpowers/plans/2026-08-25-uart-sbus-converter.md`

## Global Constraints

- **Repo:** Edit `zephyr-devel` only. Do not change `deps/zephyr` UART drivers or `nucleo_g431kb` board files.
- **Branch:** Create or stay on a `cursor/`-prefixed branch (example `cursor/uart-sbus-frame-pipeline`; Cloud suffix OK).
- **Board:** `nucleo_g431kb` (STM32G431KB Nucleo-32). No C031 / `sbus_c031g6` in this work.
- **Console:** Keep ST-Link VCP on LPUART1 (PA2/PA3). Do not steal it. Do not mux PA2/A7 as GPIO.
- **Aliases:** `uart-in = &usart1`, `sbus-out = &usart2`, `sbus-err-led` for D12. Use existing `led0` (LD2 / PB8) for green.
- **UART PHY (unchanged):** USART1 RX PA10 (D0) 115200 8N1; USART2 TX PB3 (D13) 100000 8E2 `tx-invert` TX-only; NVIC USART1 `<37 0>`, USART2 `<38 1>`, LPUART1 `<91 2>`.
- **Framing:** Classic 25-byte S.BUS. Header `0x0F`. Footer valid iff `(footer & SBUS_FOOTER_MASK) == 0x00`. Default mask `0xFF`. `CONFIG_UART_SBUS_SBUS2=y` uses mask `0xCB`.
- **Path:** First-byte cut-through. Never abort in-flight `current`. No channel packing, CRSF, DMA/async UART, failsafe frames, PWM, or `spsc_lockfree`.
- **Overflow:** Two slots only. Drop a complete waiting `next` as a whole (`supersede++`) when a newly committed frame replaces it. Do not drop interior bytes of `current`.
- **ISRs:** Only `uart_irq_*` / `uart_fifo_*` / `sbus_pipe_push`/`pop` / `uart_err_check` / `atomic_inc` / `k_sem_give`. No `printk`, no `gpio_*`, no sleeps.
- **LEDs:** GPIO only from the main thread. Green: 50 ms pulse when `tx_frames / 10` increases. Red: max(remaining, 200 ms) on supersede; max(remaining, 2000 ms) on `sync_err` or `rx_err`; not sticky.
- **Stats:** Every ~10 s from the same main loop: `sbus: rx=%u tx=%u err=%u frames=%u fps=%u sup=%u sync=%u` with wrap-safe `fps = tx_frames_delta / 10` (nominal 10 s). No `tx_bytes/25` estimate. No `drops`.
- **Tests:** Share `samples/uart_sbus/src/sbus_pipe.c` with `tests/sbus_pipe`. Two twister cases. Do not run the full converter on `native_sim`. Do not test Zephyr GPIO/USART drivers in `tests/sbus_pipe`.
- **Build:** `ZEPHYR_BASE=<repo>/deps/zephyr`, prefix west with `uv run`.
- **Commits:** Conventional Commits. Do not use `git commit -s` / `--signoff`. Do not commit secrets.

---

## File map

| Path | Role |
|---|---|
| `Kconfig` | Module `CONFIG_UART_SBUS_SBUS2` (default `n`) |
| `zephyr/module.yml` | `build.kconfig: Kconfig` |
| `samples/uart_sbus/src/sbus_pipe.h` | Two-slot assembler API + atomics (no SPSC) |
| `samples/uart_sbus/src/sbus_pipe.c` | Hunt/collect/commit/promote |
| `tests/sbus_pipe/src/main.c` | ztest of the same `.c` |
| `tests/sbus_pipe/CMakeLists.txt` | Unchanged: compiles sample `sbus_pipe.c` |
| `tests/sbus_pipe/prj.conf` | Unchanged: `CONFIG_ZTEST=y` |
| `tests/sbus_pipe/tests.yaml` | `sbus.pipe` and `sbus.pipe.sbus2` on `native_sim` |
| `samples/uart_sbus/src/main.c` | IRQ glue, `k_sem` wake, 5 ms LED loop, 10 s stats |
| `samples/uart_sbus/prj.conf` | Add `CONFIG_GPIO=y` |
| `samples/uart_sbus/boards/nucleo_g431kb.overlay` | Keep UART; add D12 `sbus-err-led` |
| `samples/uart_sbus/README.rst` | Framing, D12 5 V LED, LD2, SBUS2 Kconfig, stats |
| `samples/uart_sbus/img/wiring.svg` | Mark D12 error LED |
| `samples/uart_sbus/CMakeLists.txt` | Unchanged |
| `samples/uart_sbus/sample.yaml` | Unchanged: `nucleo_g431kb` `build_only` |

**Implementation-only fields** (not in the spec struct dump, required to honor commit-time supersede with two slots): `collect` (slot being filled), `rx_win[25]` (window bytes while assembling into `next` so a complete waiting `next` is not overwritten until a good footer). `collect_idx` is the window fill index as in the spec.

---

### Task 1: Two-slot assembler + native_sim ztest

**Files:**
- Create: `Kconfig`
- Modify: `zephyr/module.yml`
- Modify: `samples/uart_sbus/src/sbus_pipe.h`
- Modify: `samples/uart_sbus/src/sbus_pipe.c`
- Modify: `tests/sbus_pipe/src/main.c`
- Modify: `tests/sbus_pipe/tests.yaml`
- Test: `tests/sbus_pipe/` (existing CMake already compiles sample `sbus_pipe.c`)

**Interfaces:**
- Consumes: `<zephyr/sys/atomic.h>`, `<zephyr/irq.h>` (`irq_lock` / `irq_unlock`), `IS_ENABLED(CONFIG_UART_SBUS_SBUS2)`
- Produces:
  - `#define SBUS_FRAME_LEN 25`, `SBUS_HDR 0x0F`, `SBUS_FTR 0x00`
  - `#define SBUS_FOOTER_MASK (IS_ENABLED(CONFIG_UART_SBUS_SBUS2) ? 0xCBu : 0xFFu)`
  - `enum sbus_rx_st { SBUS_RX_HUNT, SBUS_RX_COLLECT }`
  - `struct sbus_slot { uint8_t buf[25]; uint8_t len; uint8_t rd; bool complete; bool tx_started; }`
  - `struct sbus_pipe` with `slots[2]`, `current`, `next`, `collect`, `rx_st`, `collect_idx`, `rx_win[25]`, atomics `rx_bytes`, `tx_bytes`, `rx_err`, `rx_frames`, `tx_frames`, `supersede`, `sync_err`
  - `void sbus_pipe_init(struct sbus_pipe *p)`
  - `bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b)` — `true` if the byte was accepted into a COLLECT window (including the header). Hunt noise returns `false` and does **not** increment `sync_err`. Increment `rx_bytes` on every accepted COLLECT byte. On a good 25th byte: `complete` on the dest slot, `rx_frames++`; if dest was `next` and `next` was already complete, `supersede++` then replace `next` only. On a bad 25th byte: `sync_err++`; reset incomplete `next` if that was the dest and `!tx_started`; leave in-flight `current` bytes for TX.
  - `bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b)` — `true` if `*b` set; first successful pop sets `tx_started`; `tx_bytes++`. When `current` is drained (`rd == len` and RX is not still COLLECT on `current`): if `complete` then `tx_frames++`; then promote (even if `complete` is false). Do not promote an idle empty slot (`len == 0 && !tx_started && !complete`).
  - `bool sbus_pipe_is_empty(const struct sbus_pipe *p)` — `true` when `current->rd == current->len` (TX has caught up; gap during cut-through COLLECT is empty from TX’s view).
- Removes: `SBUS_PIPE_CAP`, `SPSC_DECLARE`, `rx_drops`, `spsc_*`.

- [ ] **Step 1: Register module Kconfig and write the failing tests**

Create `Kconfig`:

```kconfig
# SPDX-License-Identifier: Apache-2.0

config UART_SBUS_SBUS2
	bool "Accept S.BUS2 slot footers"
	default n
	help
	  Valid footer iff (byte & mask) == 0x00.
	  n: mask 0xFF (classic S.BUS, footer 0x00 only).
	  y: mask 0xCB (ignore bits 2,4,5: 0x00/0x04/0x14/0x24/0x34).
```

Replace `zephyr/module.yml` with:

```yaml
name: zephyr-devel
build:
  kconfig: Kconfig
samples:
  - samples
tests:
  - tests
```

Replace `tests/sbus_pipe/tests.yaml` with:

```yaml
common:
  platform_allow:
    - native_sim
  integration_platforms:
    - native_sim
  tags:
    - sbus
tests:
  sbus.pipe: {}
  sbus.pipe.sbus2:
    extra_configs:
      - CONFIG_UART_SBUS_SBUS2=y
```

Replace `tests/sbus_pipe/src/main.c` with:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

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

ZTEST(sbus_pipe, test_empty_pop_fails)
{
	uint8_t b = 0xAA;

	zassert_true(sbus_pipe_is_empty(&pipe));
	zassert_false(sbus_pipe_pop(&pipe, &b));
	zassert_equal(b, 0xAA);
	zassert_equal(atomic_get(&pipe.tx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 0);
	zassert_equal(atomic_get(&pipe.rx_frames), 0);
	zassert_equal(pipe.rx_st, SBUS_RX_HUNT);
}

ZTEST(sbus_pipe, test_hunt_ignores_noise)
{
	zassert_false(sbus_pipe_push(&pipe, 0x00));
	zassert_false(sbus_pipe_push(&pipe, 0xAA));
	zassert_false(sbus_pipe_push(&pipe, 0x0E));
	zassert_equal(atomic_get(&pipe.sync_err), 0);
	zassert_equal(atomic_get(&pipe.rx_bytes), 0);
	zassert_equal(pipe.rx_st, SBUS_RX_HUNT);

	zassert_true(sbus_pipe_push(&pipe, SBUS_HDR));
	zassert_equal(pipe.rx_st, SBUS_RX_COLLECT);
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
	zassert_equal(pipe.rx_st, SBUS_RX_HUNT);
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
	zassert_equal(pipe.rx_st, SBUS_RX_HUNT);
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
	zassert_equal(pipe.collect_idx, 2);
	zassert_false(pipe.next->complete);
}

ZTEST(sbus_pipe, test_supersede_keeps_newest_next)
{
	push_frame(0xA1, SBUS_FTR);
	drain_n(1);

	push_frame(0xB2, SBUS_FTR);
	zassert_equal(atomic_get(&pipe.supersede), 0);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xB2);

	push_frame(0xC3, SBUS_FTR);
	zassert_equal(atomic_get(&pipe.supersede), 1);
	zassert_true(pipe.next->complete);
	zassert_equal(pipe.next->buf[1], 0xC3);
	zassert_equal(pipe.current->buf[1], 0xA1);
	zassert_equal(atomic_get(&pipe.rx_frames), 3);
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
```

Leave `tests/sbus_pipe/CMakeLists.txt` and `prj.conf` unchanged.

- [ ] **Step 2: Run tests to verify they fail**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west twister -T tests/sbus_pipe -p native_sim -v
```

Expected: FAIL at compile (header still has `SBUS_PIPE_CAP` / SPSC; tests refer to `SBUS_FRAME_LEN`, `pipe.current`, `rx_frames`, `SBUS_RX_HUNT`).

- [ ] **Step 3: Implement the two-slot assembler**

Replace `samples/uart_sbus/src/sbus_pipe.h` with:

```c
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

enum sbus_rx_st {
	SBUS_RX_HUNT,
	SBUS_RX_COLLECT,
};

struct sbus_pipe {
	struct sbus_slot slots[2];
	struct sbus_slot *current;
	struct sbus_slot *next;
	struct sbus_slot *collect; /* slot this COLLECT window targets; NULL in HUNT */
	enum sbus_rx_st rx_st;
	uint8_t collect_idx;
	uint8_t rx_win[SBUS_FRAME_LEN]; /* next-slot window; preserves complete next */

	atomic_t rx_bytes;
	atomic_t tx_bytes;
	atomic_t rx_err;
	atomic_t rx_frames;
	atomic_t tx_frames;
	atomic_t supersede;
	atomic_t sync_err;
};

void sbus_pipe_init(struct sbus_pipe *p);
bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b);
bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b);
bool sbus_pipe_is_empty(const struct sbus_pipe *p);

#endif /* SBUS_PIPE_H_ */
```

Replace `samples/uart_sbus/src/sbus_pipe.c` with:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/irq.h>

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
	return p->rx_st == SBUS_RX_COLLECT && p->collect == p->current;
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

static void sbus_promote(struct sbus_pipe *p)
{
	unsigned int key = irq_lock();
	struct sbus_slot *drained = p->current;

	p->current = p->next;
	p->next = drained;
	sbus_slot_reset(p->next);
	irq_unlock(key);
}

void sbus_pipe_init(struct sbus_pipe *p)
{
	sbus_slot_reset(&p->slots[0]);
	sbus_slot_reset(&p->slots[1]);
	p->current = &p->slots[0];
	p->next = &p->slots[1];
	p->collect = NULL;
	p->rx_st = SBUS_RX_HUNT;
	p->collect_idx = 0;
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
	if (p->rx_st == SBUS_RX_HUNT) {
		if (b != SBUS_HDR) {
			return false;
		}
		p->collect = sbus_slot_owes_tx(p->current) ? p->next : p->current;
		p->collect_idx = 0;
		p->rx_st = SBUS_RX_COLLECT;
	}

	p->rx_win[p->collect_idx] = b;
	if (p->collect == p->current) {
		p->current->buf[p->current->len] = b;
		p->current->len++;
	}
	p->collect_idx++;
	atomic_inc(&p->rx_bytes);

	if (p->collect_idx < SBUS_FRAME_LEN) {
		return true;
	}

	if (sbus_footer_ok(p->rx_win[SBUS_FRAME_LEN - 1])) {
		if (p->collect == p->next) {
			if (p->next->complete) {
				atomic_inc(&p->supersede);
			}
			memcpy(p->next->buf, p->rx_win, SBUS_FRAME_LEN);
			p->next->len = SBUS_FRAME_LEN;
			p->next->rd = 0;
			p->next->tx_started = false;
			p->next->complete = true;
		} else {
			p->current->complete = true;
		}
		atomic_inc(&p->rx_frames);
	} else {
		atomic_inc(&p->sync_err);
		if (p->collect == p->next && !p->next->complete && !p->next->tx_started) {
			sbus_slot_reset(p->next);
		}
	}

	p->rx_st = SBUS_RX_HUNT;
	p->collect = NULL;
	p->collect_idx = 0;
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

```

Do not increment `rx_err` here; IRQ glue owns that counter. Do not `gpio_*` or `printk`. Do not promote while COLLECT is still appending to `current` (`sbus_current_drained` is false in the cut-through gap). Write `next` only on a **good** footer so a failed replacement keeps a complete waiting `next`.

- [ ] **Step 4: Run the tests and confirm they pass**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west twister -T tests/sbus_pipe -p native_sim -v
```

Expected: `sbus.pipe` PASS and `sbus.pipe.sbus2` PASS on `native_sim` (12 tests each). In the default case, `test_footer_0x04_and_0x14` expects `sync_err`; in `sbus2`, it expects `rx_frames`.

- [ ] **Step 5: Commit**

```bash
git add Kconfig zephyr/module.yml \
  samples/uart_sbus/src/sbus_pipe.h samples/uart_sbus/src/sbus_pipe.c \
  tests/sbus_pipe/src/main.c tests/sbus_pipe/tests.yaml
git commit -m "$(cat <<'EOF'
feat(sbus): replace byte pipe with two-slot frame assembler

Classic 25-byte hunt/collect with cut-through into current, in-flight
assembly into next, commit-time supersede, and irq_lock promote.
Share the same .c with native_sim ztest cases for SBUS2 off and on.
EOF
)"
```

---

### Task 2: IRQ glue, LEDs, overlay, README

**Files:**
- Modify: `samples/uart_sbus/src/main.c`
- Modify: `samples/uart_sbus/prj.conf`
- Modify: `samples/uart_sbus/boards/nucleo_g431kb.overlay`
- Modify: `samples/uart_sbus/README.rst`
- Modify: `samples/uart_sbus/img/wiring.svg`

**Interfaces:**
- Consumes: `sbus_pipe_*` from Task 1; `DT_ALIAS(uart_in)`, `DT_ALIAS(sbus_out)`, `DT_ALIAS(led0)`, `DT_ALIAS(sbus_err_led)`; binary `k_sem wake`
- Produces: sample on `nucleo_g431kb`; LD2 50 ms pulse per 10 `tx_frames`; D12 open-drain error LED; console line `sbus: rx=%u tx=%u err=%u frames=%u fps=%u sup=%u sync=%u`; twister `sample.uart_sbus` build-only still passes

- [ ] **Step 1: Overlay, GPIO Kconfig, and IRQ/LED `main.c`**

Keep USART nodes and NVIC in `samples/uart_sbus/boards/nucleo_g431kb.overlay`. Replace the `/ { aliases ... }` block so the file is:

```dts
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * NUCLEO-G431KB (UM2397) Arduino Nano mapping, not Nucleo-64:
 *   ST-Link VCP is LPUART1 PA2/PA3 (not D0/D1).
 *   UART in:  USART1 RX PA10 = D0 (CN4 pin 2).
 *   S.BUS:    USART2 TX PB3  = D13 (CN3 pin 15).
 *   Error LED: GPIOB 4 = D12 (CN4 pin 15), open-drain active-low to 5 V.
 * D7 is PF0 (OSC_IN) and D3 is PB0; neither is a USART pin.
 * Default SB2 routes PB7 to A4, not to D4, so USART1_RX_PB7 is a trap.
 * Do not use A7 (PA2): LPUART1 TX / ST-Link VCP, not 5 V-tolerant.
 */

#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
	aliases {
		uart-in = &usart1;
		sbus-out = &usart2;
		sbus-err-led = &sbus_err_led;
	};

	sbus_err_leds {
		compatible = "gpio-leds";
		sbus_err_led: sbus_err_led {
			gpios = <&gpiob 4 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>;
			label = "S.BUS error";
		};
	};
};

&usart1_rx_pa10 {
	bias-pull-up;
};

&usart1 {
	pinctrl-0 = <&usart1_rx_pa10>;
	pinctrl-names = "default";
	current-speed = <115200>;
	fifo-enable;
	interrupts = <37 0>;
	status = "okay";
};

&usart2 {
	pinctrl-0 = <&usart2_tx_pb3>;
	pinctrl-names = "default";
	current-speed = <100000>;
	parity = "even";
	stop-bits = "2";
	tx-invert;
	fifo-enable;
	interrupts = <38 1>;
	status = "okay";
};

&lpuart1 {
	interrupts = <91 2>;
};
```

Replace `samples/uart_sbus/prj.conf` with:

```
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_GPIO=y
```

Replace `samples/uart_sbus/src/main.c` with:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "sbus_pipe.h"

#define UART_IN_NODE     DT_ALIAS(uart_in)
#define SBUS_OUT_NODE    DT_ALIAS(sbus_out)
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(sbus_err_led)

#define MAIN_TICK_MS       5
#define GREEN_PULSE_MS     50
#define RED_SUPERSEDE_MS   200
#define RED_FAULT_MS       2000
#define STATS_PERIOD_MS    10000

BUILD_ASSERT(DT_NODE_EXISTS(UART_IN_NODE), "alias uart-in missing");
BUILD_ASSERT(DT_NODE_EXISTS(SBUS_OUT_NODE), "alias sbus-out missing");
BUILD_ASSERT(DT_NODE_EXISTS(LED_GREEN_NODE), "alias led0 missing");
BUILD_ASSERT(DT_NODE_EXISTS(LED_RED_NODE), "alias sbus-err-led missing");

static const struct device *const uart_in = DEVICE_DT_GET(UART_IN_NODE);
static const struct device *const sbus_out = DEVICE_DT_GET(SBUS_OUT_NODE);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

static struct sbus_pipe pipe;
K_SEM_DEFINE(wake, 0, 1);

static void wake_if_changed(atomic_val_t before, atomic_t *counter)
{
	if (atomic_get(counter) != before) {
		k_sem_give(&wake);
	}
}

static void uart_in_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	int err;
	atomic_val_t sup0;
	atomic_val_t sync0;
	atomic_val_t err0;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	err0 = atomic_get(&pipe.rx_err);
	err = uart_err_check(dev);
	if (err != 0) {
		atomic_inc(&pipe.rx_err);
	}

	sup0 = atomic_get(&pipe.supersede);
	sync0 = atomic_get(&pipe.sync_err);

	while (uart_irq_rx_ready(dev) > 0) {
		if (uart_fifo_read(dev, &c, 1) < 1) {
			break;
		}
		if (sbus_pipe_push(&pipe, c)) {
			uart_irq_tx_enable(sbus_out);
		}
	}

	wake_if_changed(err0, &pipe.rx_err);
	wake_if_changed(sup0, &pipe.supersede);
	wake_if_changed(sync0, &pipe.sync_err);
}

static void sbus_out_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	atomic_val_t txf0;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	txf0 = atomic_get(&pipe.tx_frames);

	while (uart_irq_tx_ready(dev) > 0) {
		if (!sbus_pipe_pop(&pipe, &c)) {
			uart_irq_tx_disable(dev);
			break;
		}
		if (uart_fifo_fill(dev, &c, 1) < 1) {
			uart_irq_tx_disable(dev);
			break;
		}
	}

	wake_if_changed(txf0, &pipe.tx_frames);
}

static uint16_t remaining_sub(uint16_t remaining, uint16_t dt)
{
	return remaining > dt ? remaining - dt : 0;
}

int main(void)
{
	uint32_t tx_frames_prev = 0;
	uint32_t tx_frames_snap = 0;
	uint32_t supersede_snap = 0;
	uint32_t sync_err_snap = 0;
	uint32_t rx_err_snap = 0;
	uint16_t green_ms = 0;
	uint16_t red_ms = 0;
	int64_t last_led = k_uptime_get();
	int64_t last_stats = last_led;

	sbus_pipe_init(&pipe);

	if (!device_is_ready(uart_in) || !device_is_ready(sbus_out)) {
		printk("sbus: UART device not ready\n");
		return 0;
	}
	if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
		printk("sbus: LED GPIO not ready\n");
		return 0;
	}
	if (gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("sbus: LED configure failed\n");
		return 0;
	}

	uart_irq_callback_user_data_set(uart_in, uart_in_cb, NULL);
	uart_irq_callback_user_data_set(sbus_out, sbus_out_cb, NULL);
	uart_irq_err_enable(uart_in);
	uart_irq_rx_enable(uart_in);

	printk("sbus: D0/PA10 115200 8N1 -> D13/PB3 S.BUS 100k 8E2\n");

	for (;;) {
		int64_t now;
		uint16_t dt;
		uint32_t rx;
		uint32_t tx;
		uint32_t err;
		uint32_t frames;
		uint32_t sup;
		uint32_t sync;
		uint32_t tx_frames;

		k_sem_take(&wake, K_MSEC(MAIN_TICK_MS));
		now = k_uptime_get();
		dt = (uint16_t)CLAMP(now - last_led, 0, 1000);
		last_led = now;

		tx_frames = (uint32_t)atomic_get(&pipe.tx_frames);
		if ((tx_frames / 10U) > (tx_frames_snap / 10U)) {
			green_ms = GREEN_PULSE_MS;
		}
		tx_frames_snap = tx_frames;

		sup = (uint32_t)atomic_get(&pipe.supersede);
		if (sup != supersede_snap) {
			red_ms = MAX(red_ms, RED_SUPERSEDE_MS);
		}
		supersede_snap = sup;

		sync = (uint32_t)atomic_get(&pipe.sync_err);
		err = (uint32_t)atomic_get(&pipe.rx_err);
		if (sync != sync_err_snap || err != rx_err_snap) {
			red_ms = MAX(red_ms, RED_FAULT_MS);
		}
		sync_err_snap = sync;
		rx_err_snap = err;

		if (green_ms > 0) {
			gpio_pin_set_dt(&led_green, 1);
			green_ms = remaining_sub(green_ms, dt);
		} else {
			gpio_pin_set_dt(&led_green, 0);
		}
		if (red_ms > 0) {
			gpio_pin_set_dt(&led_red, 1);
			red_ms = remaining_sub(red_ms, dt);
		} else {
			gpio_pin_set_dt(&led_red, 0);
		}

		if ((now - last_stats) >= STATS_PERIOD_MS) {
			uint32_t fps;

			rx = (uint32_t)atomic_get(&pipe.rx_bytes);
			tx = (uint32_t)atomic_get(&pipe.tx_bytes);
			frames = tx_frames;
			fps = (tx_frames - tx_frames_prev) / 10U;
			tx_frames_prev = tx_frames;
			last_stats = now;
			printk("sbus: rx=%u tx=%u err=%u frames=%u fps=%u sup=%u sync=%u\n",
			       rx, tx, err, frames, fps, sup, sync);
		}
	}
}
```

Do not call `uart_irq_rx_enable(sbus_out)`. Do not use `uart_poll_*` or UART async APIs. Do not `gpio_*` from ISRs. After every successful `sbus_pipe_push`, call `uart_irq_tx_enable(sbus_out)` even if TX IRQ is already enabled. Decay LEDs with `k_uptime` delta so an early `k_sem_give` does not accelerate 50/200/2000 ms. Apply supersede then fault in that order so a later 200 ms pulse cannot shorten a 2 s fault (`MAX`).

- [ ] **Step 2: README and wiring figure**

Replace `samples/uart_sbus/README.rst` with:

```rst
UART to S.BUS converter
#######################

Cut-through converter: classic 25-byte S.BUS frames arriving on ordinary
115200 8N1 UART are emitted on an inverted 100 kbit/s 8E2 S.BUS UART.
Transmission starts on the first header byte (``0x0F``), not after the
footer. A locked 25-byte window follows each header; payload ``0x0F``
does not resync. A complete frame still waiting to start TX is dropped
as a whole when a newer valid frame is committed.

Supported board: ``nucleo_g431kb``.

Wiring (Nucleo-32 Arduino Nano header)
**************************************

.. figure:: img/wiring.svg
   :align: center
   :alt: Nucleo-32 G431KB with USB at top. UART in on D0. S.BUS out on D13.
         Red error LED on D12 to 5 V. Console is ST-Link USB, not D0/D1.

   Nucleo-32 pinout (USB / ST-LINK at the top) per ST UM2397. D0/D1 are USART1,
   not the ST-Link VCP. VCP is LPUART1 on PA2/PA3, wired only to the debugger.

- Console: ST-Link VCP (USB serial), LPUART1 PA2/PA3, 115200 8N1. Not Arduino D0/D1.
- Input: USART1 RX **PA10 (Arduino D0, CN4 pin 2)**, 115200 8N1
- S.BUS output: USART2 TX **PB3 (Arduino D13, CN3 pin 15)**, 100000 8E2,
  hardware ``tx-invert``, TX-only. Idle is low. No external inverter.
- Green activity: onboard **LD2 (PB8)**, 50 ms pulse every 10 transmitted frames
- Red error: external LED on **D12 / PB4 (CN4 pin 15)**, GPIO open-drain
  active-low. Wire **+5 V (CN3 pin 4)** → ~330 Ω → LED anode → cathode to D12.
  200 ms on supersede, 2 s on UART/sync faults (not sticky; retrigger uses max
  remaining vs new duration).
- Tie companion UART, Nucleo, and S.BUS device **GND** together.
- Do not use D7 or D3: D7 is PF0 (OSC_IN) and D3 is PB0. Neither is a USART pin.
- Do not use A7 for the LED. A7 is PA2 (LPUART1 TX / ST-Link VCP) and is not
  5 V-tolerant.

Building and flashing
*********************

.. code-block:: console

   export ZEPHYR_BASE=$PWD/deps/zephyr
   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
   uv run west flash -d /tmp/b_uart_sbus --runner openocd

Optional Futaba S.BUS2 slot footers (``0x04`` / ``0x14`` / ``0x24`` / ``0x34``)::

   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus -- -DCONFIG_UART_SBUS_SBUS2=y

Default is classic S.BUS only (footer ``0x00``). Inter-window telemetry bytes
are always dropped while hunting; they do not increment ``sync``.

Stats
*****

Every 10 seconds the console prints lifetime counters and frames/s for that
interval (``tx_frames`` delta / 10):

.. code-block:: console

   sbus: rx=25000 tx=25000 err=0 frames=1000 fps=100 sup=0 sync=0

``frames`` is lifetime fully transmitted valid S.BUS frames. ``sup`` is
waiting frames dropped as stale. ``sync`` is bad footers at the end of a
25-byte window. A bad footer after cut-through still emits the bytes already
on the wire; the incomplete ``current`` is drained and promoted. If input
stops, S.BUS goes idle; the flight controller must apply its own failsafe.
```

In `samples/uart_sbus/img/wiring.svg`, change the D12 circle and add a left-side label (same style as “UART in”):

- Circle at `cx="258" cy="634"`: `fill="#fc8181"`
- After the D12 pin label text, add:

```xml
<text x="244" y="634" text-anchor="end" dominant-baseline="central" font-family="sans-serif" font-size="13" font-weight="700" fill="#c53030">Error LED</text>
<text x="244" y="654" text-anchor="end" font-family="sans-serif" font-size="11" fill="#c53030">D12/PB4 OD to 5V</text>
```

- Update `<title>` / `<desc>` to mention D12 error LED.
- Change the `+5V` pin label on CN3 (`cx="522" cy="260"`) so the caption under the figure in README stays accurate; add after the existing +5V pin text:

```xml
<text x="536" y="264" text-anchor="start" font-family="sans-serif" font-size="11" fill="#c53030">red LED anode via 330Ω</text>
```

- [ ] **Step 3: Build the sample and re-run assembler tests**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
```

Expected: ninja succeeds; no missing alias / GPIO configure errors.

```bash
uv run west twister -T samples/uart_sbus -p nucleo_g431kb --build-only
```

Expected: `sample.uart_sbus` PASS (built).

```bash
uv run west twister -T tests/sbus_pipe -p native_sim
```

Expected: `sbus.pipe` and `sbus.pipe.sbus2` still PASS.

Hardware check (manual, not a CI gate): 115200 8N1 into D0; inverted 100 kbit/s 8E2 on D13 starting after the first input byte (not after 25); LD2 pulses 10 times/s at 100 frames/s (50 ms on); burst faster than S.BUS TX → red 200 ms; unplug/noise → red 2 s; ST-Link console still works; stats line every ~10 s with `frames`/`fps`/`sup`/`sync`.

- [ ] **Step 4: Commit**

```bash
git add samples/uart_sbus/src/main.c samples/uart_sbus/prj.conf \
  samples/uart_sbus/boards/nucleo_g431kb.overlay \
  samples/uart_sbus/README.rst samples/uart_sbus/img/wiring.svg
git commit -m "$(cat <<'EOF'
feat(sbus): add LED FSMs and framed stats to UART sample

Wake main from ISRs with a binary semaphore. Pulse LD2 every 10 TX
frames; drive D12 open-drain for supersede (200 ms) and faults (2 s).
EOF
)"
```

---

## Spec coverage

| Spec section | Task |
|---|---|
| §1 Goals 1–3 hunt/collect/footer/SBUS2 | Task 1 tests + `SBUS_FOOTER_MASK` |
| §1 Goals 4–6 cut-through, no abort, supersede | Task 1 push/pop/promote |
| §1 Goals 7–9 LEDs + main-thread GPIO | Task 2 `main.c` |
| §1 Non-goals (no SPSC, no C031, no DMA) | Global constraints; SPSC removed in Task 1 |
| §2 Pins D0/D13, LD2, D12, not A7 | Task 2 overlay + README |
| §3 Architecture ISR → assembler → main | Task 1 pipe, Task 2 glue |
| §4 Two-slot assembler, commit, promote | Task 1 |
| §5 IRQ glue, `k_sem_give`, no GPIO in ISR | Task 2 `main.c` |
| §6 LED FSMs, 5 ms cycle, stats format | Task 2 `main.c` + README |
| §7 DT aliases + module Kconfig | Task 1 `Kconfig`; Task 2 overlay/`prj.conf` |
| §8 File layout, shared `.c` | Both tasks |
| §9 ztest cases + build-only twister | Task 1 / Task 2 |
| §10 Safety (corrupt burst, idle, FT D12) | Task 2 README |

## Notes for the implementer

- `rx_win` exists so a complete waiting `next` survives a **bad** replacement window. Supersede increments only when a **good** footer is committed into an already-complete `next`.
- `fps` uses integer `tx_frames_delta / 10` for the nominal 10 s period, not elapsed-ms or `tx_bytes / 25`.
- Green uses `tx_frames / 10` (integer). One pulse per tick even if the quotient jumps by more than 1.
- Red remaining time is `MAX(remaining, new)` so a 2 s fault is never shortened by a later supersede.
- Existing V1 tests (`test_drop_newest_keeps_queued`, `test_wrap_around`, `rx_drops`) are deleted with the byte ring.
