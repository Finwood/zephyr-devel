# UART to S.BUS Converter — Frame Pipeline and LEDs

- **Date:** 2026-08-28
- **Status:** Approved design, pre-implementation
- **Plan:** `docs/superpowers/plans/2026-08-28-uart-sbus-frame-pipeline.md`
- **Scope:** Evolve `samples/uart_sbus` on `nucleo_g431kb`: classic 25-byte
S.BUS framing, two-slot cut-through with stale-frame drop, green activity
LED, decaying red error LED. `zephyr-devel` module only.
- **Supersedes (path only):** V1 byte-pipe behavior in
`docs/superpowers/specs/2026-08-25-uart-sbus-converter-design.md`.
Physical layer, pins D0/D13, NVIC, and “no driver changes” still apply.
- **Upstream target:** none. No in-tree Zephyr UART or `nucleo_g431kb`
board-file changes.

**Parent spec (V1, landed):**
`docs/superpowers/specs/2026-08-25-uart-sbus-converter-design.md`

---



## 1. Purpose

V1 is a transparent 64-byte SPSC pipe. Bursts larger than the queue drop
**bytes**, which desynchronizes S.BUS consumers. On-target use showed that
the companion can deliver a new frame while a previous complete frame is
still waiting to start TX. In that case the waiting frame is stale RC data
and must be discarded as a whole.

This revision keeps first-byte cut-through and the inverted 100 kbit/s 8E2
PHY. It replaces the byte ring with a **two-slot frame assembler**, adds
header/footer checks, and adds LEDs driven only from the main thread.

### Goals

1. Recognize classic S.BUS frames: 25 bytes, header `0x0F`, footer valid iff
   `(footer & SBUS_FOOTER_MASK) == 0x00` (mask `0xFF` by default).
2. Optional S.BUS2 slot footers via `CONFIG_UART_SBUS_SBUS2` (default `n`).
3. Locked 25-byte window after a header; hunt `0x0F` only when unsynced or
   after a completed/failed window (payload `0x0F` must not resync).
4. Cut-through: S.BUS TX starts on the first byte of a frame when the line
   is idle; do not wait for the footer before enabling TX.
5. Never abort a frame that has started TX (`current` in-flight).
6. If a new header is committed while `next` already holds a **complete**
   valid frame that has not started TX, drop that waiting frame
   (supersede) and keep only the new one.
7. Green onboard LD2: 50 ms pulse every 10 **transmitted** frames.
8. Red external LED on D12: open-drain, active-low, from 5 V; 200 ms on
   supersede, 2 s on UART/sync faults; not sticky; retrigger uses max
   remaining vs new duration.
9. All GPIO/LED policy in the main thread (1–10 ms cycle, default 5 ms),
   woken by one ISR semaphore plus timeout. ISR path stays UART + assembler
   + atomics + `k_sem_give`.



### Non-goals

- Channel packing, CRSF/ELRS, failsafe/hold-last frames.
- Parsing S.BUS2 **telemetry bytes** between windows (HUNT still drops
  non-`0x0F` with no `sync_err`). Slot footers are opt-in Kconfig only.
- DMA / UART async API; PWM dimming (D12 remains `TIM3_CH1`-capable).
- Aborting in-flight TX; dropping interior bytes of `current`.
- `spsc_lockfree` byte ring (removed).
- C031 / `sbus_c031g6` in this spec.
- Changes to in-tree Zephyr UART drivers or `nucleo_g431kb` board files.
- Running the full converter on `native_sim`.

---



## 2. Physical layer and pins (unchanged UART, new LED)


| Side           | Baud / drive                    | Nucleo-32 pin (UM2397)           |
| -------------- | ------------------------------- | -------------------------------- |
| Console        | 115200 8N1 LPUART1              | PA2/PA3 ST-Link VCP only         |
| Input          | 115200 8N1 USART1 RX            | PA10 Arduino **D0** (CN4 pin 2)  |
| S.BUS out      | 100000 8E2 inverted USART2 TX   | PB3 Arduino **D13** (CN3 pin 15) |
| Green activity | GPIO push-pull, active-high     | **LD2 / PB8** (`led0`)           |
| Red error      | GPIO **open-drain, active-low** | **D12 / PB4** (CN4 pin 15)       |


Red LED wiring: **+5 V** (CN3 pin 4) → series resistor (~330 Ω) → LED
anode → cathode to **D12**. The pad sinks to GND when on, Hi-Z when off.

**Do not use A7 for the LED.** A7 is PA2: LPUART1 TX / ST-Link VCP (SB1)
and datasheet I/O **TT_a** (not 5 V-tolerant). Open-drain to 5 V on A7 can
damage the pin and steals the console.

**D12 / PB4** is **FT_c** (5 V-tolerant) and `TIM3_CH1` if PWM is added
later. D7/D8 are PF0/PF1 (OSC). D2 is PA12 with a factory D2–GND jumper.

NVIC, `tx-invert`, `fifo-enable`, and USART aliases stay as in V1.

---



## 3. Architecture

```text
USART1 RX IRQ  →  assembler (hunt / collect / commit)
                      │
              slots[2]  current ptr  /  next ptr
                      │
USART2 TX IRQ  ←  pop current; promote (irq_lock + pointer swap)
                      │
              atomics + k_sem_give(&wake)   // only ISR→main ABI
                      │
              main, 5 ms cycle: LED FSMs; printk every 10 s
                    LD2 green, D12 red, LPUART1 console
```

Three units:

1. `sbus_pipe` — two 25-byte slots, current/next pointers, RX state
  machine, commit/supersede/promote, atomics. Unit-tested on `native_sim`.
2. **IRQ glue (**`main` **ISRs)** — interrupt UART API as in V1; push/pop the
  assembler; `k_sem_give` on relevant atomic bumps. No GPIO, no `printk`.
3. **Main loop** — `k_sem_take(..., K_MSEC(5))`, snapshot atomics, LED
  policy, 10 s stats. Not on the S.BUS byte path.

USART1 still preempts USART2 (priorities 0 then 1). Console stays 2.

---



## 4. Two-slot assembler

Capacity is **two frames**, not 64 bytes. Ping-pong pointers avoid a
memcpy promote (RX can preempt TX; a 25-byte copy is not atomic).

```c
#define SBUS_FRAME_LEN 25
#define SBUS_HDR       0x0F
#define SBUS_FTR       0x00
/* Off: 0xFF (exact 0x00). On: 0xCB, ignore Futaba slot bits 2,4,5. */
#define SBUS_FOOTER_MASK (IS_ENABLED(CONFIG_UART_SBUS_SBUS2) ? 0xCBu : 0xFFu)

struct sbus_slot {
	uint8_t buf[SBUS_FRAME_LEN];
	uint8_t len;        /* bytes valid (RX write index) */
	uint8_t rd;         /* TX read index */
	bool complete;      /* len==25 and footer passed the mask */
	bool tx_started;    /* at least one byte given to USART FIFO */
};

struct sbus_pipe {
	struct sbus_slot slots[2];
	struct sbus_slot *current;  /* TX drains this */
	struct sbus_slot *next;     /* RX fills this when current is busy */
	enum { SBUS_RX_HUNT, SBUS_RX_COLLECT } rx_st;
	uint8_t collect_idx;

	atomic_t rx_bytes;
	atomic_t tx_bytes;
	atomic_t rx_err;       /* UART HW errors (glue) */
	atomic_t rx_frames;    /* valid frames committed */
	atomic_t tx_frames;    /* frames fully popped to USART */
	atomic_t supersede;    /* complete waiting next dropped */
	atomic_t sync_err;     /* bad footer at end of COLLECT */
};
```

`sbus_pipe_init` points `current` at `slots[0]`, `next` at `slots[1]`,
clears both slots, hunt state, counters.

### 4.1 RX state machine (`sbus_pipe_push`)

**HUNT:** discard bytes until `0x0F`, then enter COLLECT with that header
as byte 0. Ignoring non-header bytes while hunting does **not** increment
`sync_err` (idle/noise would pin the red LED). `sync_err` increments on a
**bad footer** at the end of COLLECT.

**COLLECT:** store the next bytes into the assembly destination (below).
Do not treat interior `0x0F` as a header. At 25 bytes, the footer is
**valid** iff `(footer & SBUS_FOOTER_MASK) == SBUS_FTR`:

- Valid: mark complete, `rx_frames++`, return to HUNT.
- Invalid: `sync_err++`, reset the incomplete assembly slot if it was
  `next` and not `tx_started`, return to HUNT. If cut-through already put
  bytes into `current`, finish sending those bytes (in-flight); do not
  rewind TX.

Assembly destination:

- If `current` has no in-flight data (`rd == len` and `!tx_started` and
`!current->complete` filling, or current empty): **cut-through into**
`current`. Each byte `current->buf[len++]`, caller enables TX IRQ.
- If `current` is in-flight (`tx_started` or `rd < len` of a frame still
owed to USART): assemble into `next`.



### 4.2 Commit / supersede

When a COLLECT window completes with a good footer into `next`:

- If `next` was empty/incomplete: it becomes the waiting complete frame.
- If `next` was already **complete** (previous waiting frame, TX not
started on it): that is supersede — discard the old `next` contents,
`supersede++`, store the new frame only. `current` is untouched.

A new header is only seen after a finished window (or from hunt), so
supersede is exactly: “complete waiting `next` replaced by a newly
committed frame.”

### 4.3 TX (`sbus_pipe_pop`) and promote

While `current->rd < current->len`, return `current->buf[rd++]` and
`tx_bytes++`. First successful pop sets `tx_started`.

`current` is **drained** when `rd == len` **and** the slot is no longer
being cut-through collected into (either `complete` is true, or COLLECT
already failed/finished this window so RX will not append more). In
practice: after a good footer, `complete` is set then TX finishes the
rest; after a **bad** footer on `current`, RX stops appending (`sync_err`,
back to HUNT) and TX finishes remaining bytes. Then `rd == len` with
`complete == false` is still drained.

When drained:

- If `complete`, `tx_frames++` (green/`fps` count only valid S.BUS).
- Then **promote**, even if `complete` is false, so a bad cut-through
  frame cannot stall the pipe.

**Promote** (TX ISR, only when `current` is drained):

1. `irq_lock()`
2. Swap: drained slot becomes `next`; former `next` becomes `current`.
3. Reset the new `next` (len/rd/complete/tx_started = 0).
4. `irq_unlock()`

If after promote `current->len > 0`, keep TX IRQ enabled (cut-through of a
partial successor or send a waiting complete frame). If `current` is empty,
disable TX IRQ.

`irq_lock` in the TX ISR blocks RX for a few stores so RX never sees both
pointers aliasing the same slot.

Do not promote while `rd == len` but RX is still in COLLECT on `current`
(cut-through gap between bytes). TX IRQ disables until the next push
enables it.

### 4.4 Cut-through vs footer

Footer is known only at byte 25. Cut-through may already have placed most
of a bad frame on the wire. That is accepted. Remaining `current` bytes
still go out; then `sync_err++` and hunt; when TX finishes the slot it
**promotes** (incomplete drain). Waiting `next` is only marked complete
after a good footer, so a bad waiting frame is never promoted.

S.BUS2 slot footers (`0x04`, `0x14`, `0x24`, `0x34`) fail this check when
`CONFIG_UART_SBUS_SBUS2=n` (default) and pass when `=y` (`mask 0xCB`).
Odd junk such as `0x01` fails in both modes (mask bit 0).

---



## 5. IRQ glue

Same Zephyr interrupt UART API as V1 (`uart_irq_*` / `uart_fifo_*`). No
`uart_poll_*`, no async/DMA on USART1/USART2.

RX ISR: `uart_irq_update`; `uart_err_check` → `atomic_inc(&pipe.rx_err)`
and `k_sem_give(&wake)`; `uart_fifo_read` one byte, `sbus_pipe_push`; on
successful store that implies TX should run, `uart_irq_tx_enable`.

TX ISR: `uart_irq_update`; while `uart_irq_tx_ready`, `sbus_pipe_pop` and
`uart_fifo_fill`; empty → disable TX.

After `tx_frames`, `supersede`, `sync_err`, or `rx_err` increments, ISR
calls `k_sem_give(&wake)` (binary sem, count max 1). No GPIO.

ISR may only: UART IRQ/FIFO, pipe push/pop, `atomic_inc`, `k_sem_give`.
No `printk`, no `gpio_*`, no sleeps.

---



## 6. Main thread: LEDs and stats

Binary semaphore `wake`, given from ISRs. Main:

```text
loop:
  k_sem_take(&wake, K_MSEC(5))     /* 5 ms default in 1–10 ms band */
  snapshot atomics (atomic_get)
  update LED FSMs (5 ms tick)
  if 10 s elapsed: printk stats
```

Timeout keeps decay running if the bus is idle. 5 ms divides 50 / 200 /
2000 ms.

**Green (LD2):** if `tx_frames / 10` increased since last snapshot, start
a **50 ms** on-time (one pulse even if many frames arrived in one tick).
Drive `GPIO_ACTIVE_HIGH`. At 0, turn off.

**Red (D12):** if `supersede` increased, remaining on-time = max(remaining,
**200 ms**). If `sync_err` or `rx_err` increased, remaining = max(remaining,
**2000 ms**). Fault must not be shortened by a later supersede. Active-low
open-drain: on = drive 0, off = Hi-Z. At 0, off.

GPIO only from this loop. `led0` for green; overlay `sbus-err-led` for red.

**Stats** every ~10 s from the same loop (`k_uptime` / tick count), not a
second sleeper. Lifetime counters, wrap-safe deltas:

```text
sbus: rx=%u tx=%u err=%u frames=%u fps=%u sup=%u sync=%u
```

`fps` for the interval is `tx_frames` delta / 10 (integer, truncated; 10 is
the nominal 10 s period). No `tx_bytes/25` estimate. Keep V1 `rx`/`tx` as
lifetime byte counters and add `frames` as lifetime `tx_frames`. Example
at 100 frames/s:

```text
sbus: rx=25000 tx=25000 err=0 frames=1000 fps=100 sup=0 sync=0
```

Include `sup` and `sync` as lifetime `supersede` and `sync_err`.

---



## 7. Devicetree and Kconfig

Overlay `samples/uart_sbus/boards/nucleo_g431kb.overlay` (in addition to
V1 USART nodes):

- Keep `uart-in = &usart1`, `sbus-out = &usart2`.
- Under `/ { leds { ... } }` or a sibling `sbus-err-led` gpio-leds node:
  `gpios = <&gpiob 4 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>`.
- Alias `sbus-err-led` (and use existing `led0` for LD2).

`prj.conf`: `CONFIG_GPIO=y` if not implied. Binary `k_sem` needs no extra
event Kconfig. `CONFIG_UART_INTERRUPT_DRIVEN=y` unchanged.

Module `Kconfig` (register in `zephyr/module.yml` `build.kconfig`) so the
sample and `tests/sbus_pipe` share:

```
config UART_SBUS_SBUS2
	bool "Accept S.BUS2 slot footers"
	default n
	help
	  Valid footer iff (byte & mask) == 0x00.
	  n: mask 0xFF (classic S.BUS, footer 0x00 only).
	  y: mask 0xCB (ignore bits 2,4,5: 0x00/0x04/0x14/0x24/0x34).
```

Assembler uses `IS_ENABLED(CONFIG_UART_SBUS_SBUS2)` only to pick
`SBUS_FOOTER_MASK`. Comparison is always `(footer & mask) == 0x00`.

Sample default stays `n`. Enable with `CONFIG_UART_SBUS_SBUS2=y` in an
overlay conf or west `-D`.

Do not steal LPUART1. Do not mux PA2 as GPIO.

---



## 8. File layout


| Path | Role |
| --- | --- |
| `Kconfig` | `CONFIG_UART_SBUS_SBUS2` (module-wide) |
| `zephyr/module.yml` | `build.kconfig` |
| `samples/uart_sbus/src/sbus_pipe.h` | Slot/pointer API + atomics |
| `samples/uart_sbus/src/sbus_pipe.c` | Hunt/collect/commit/promote |
| `samples/uart_sbus/src/main.c` | IRQ glue, 5 ms LED loop, 10 s stats |
| `samples/uart_sbus/boards/nucleo_g431kb.overlay` | UART + red LED |
| `samples/uart_sbus/prj.conf` | GPIO + UART as needed |
| `samples/uart_sbus/README.rst` | D12 5 V LED, framing, LEDs, SBUS2 Kconfig |
| `tests/sbus_pipe/` | ztest of the same `sbus_pipe.c`; two twister cases |


Still compile sample `sbus_pipe.c` into the test. No duplicate assembler.

---



## 9. Testing

**Queue/assembler (CI):** `tests/sbus_pipe` on `native_sim`, **two**
twister cases sharing the same tests, `IS_ENABLED(CONFIG_UART_SBUS_SBUS2)`
for footer expectations:

- `sbus.pipe` — default `CONFIG_UART_SBUS_SBUS2=n`
- `sbus.pipe.sbus2` — `extra_configs: CONFIG_UART_SBUS_SBUS2=y`

Shared cases:

- hunt ignores noise until `0x0F`
- 25 bytes ending `0x00` → one `rx_frames`; interior `0x0F` does not resync
- footer `0x01` → always `sync_err` (fails both masks)
- footer `0x04` (and `0x14`): `sync_err` when SBUS2 is off; **valid
  commit** when SBUS2 is on
- bad footer → no complete `next`; hunt resumes; if bytes were
  cut-through on `current`, TX can drain them and **promote** (`tx_frames`
  unchanged unless that footer was valid)
- cut-through: bytes appear in `current` (`len` grows) before footer
- in-flight: `tx_started` on `current`; filling `next` does not change
  `current` bytes already queued
- second complete frame into `next` while `current` in-flight →
  `supersede++`, `next` holds only the newest frame
- promote: after drain, pointer swap; former `next` is `current`
- `tx_frames` increments once per fully popped complete frame

Do not test Zephyr GPIO or USART drivers here.

**Sample (CI):** twister build `samples/uart_sbus` for `nucleo_g431kb`,
`build_only: true`.

**Hardware (manual):** D0 in, D13 S.BUS out, D12 red LED to 5 V, LD2 green.
At 100 frames/s, green pulses 10 times per second (50 ms on). Burst faster than S.BUS
TX: red 200 ms on supersede. Unplug UART or noise: red 2 s. Console on
ST-Link VCP. Cut-through: first output byte still starts after the first
input byte, not after 25.

---



## 10. Safety notes

A bad footer after cut-through still emits a corrupt S.BUS burst; the
flight controller must tolerate that or the sender must stay well-formed.
The converter does not stall: the incomplete `current` is drained and
promoted.

With `CONFIG_UART_SBUS_SBUS2=n`, non-`0x00` footers look like a stream of
sync errors (red LED retriggered every frame) while bytes may still go
out. With the option on, Futaba slot footers `0x04`/`0x14`/`0x24`/`0x34`
are complete frames; inter-window telemetry bytes are still dropped in
HUNT.

Supersede drops a complete waiting frame on purpose. Downstream sees a
newer frame, not a spliced mix of two frames, as long as `current` is
never rewritten after `tx_started`.

Loss of input: TX disables when both slots are empty; S.BUS goes idle. No
failsafe frame.

Open-drain D12 to 5 V is valid only because PB4 is FT. Do not copy this
wiring onto TT analog pins (A7/PA2, A6/PA7, …).