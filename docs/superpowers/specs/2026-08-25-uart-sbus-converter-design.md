# UART to S.BUS Converter Sample — Design

- **Date:** 2026-08-25
- **Status:** Approved design, pre-implementation
- **Scope:** New `zephyr-devel` sample: cut-through UART (115200 8N1) to
  S.BUS physical layer (100 kbit/s, 8E2, inverted TX). Board:
  `nucleo_g431kb` (STM32G431KB Nucleo-32).
- **Upstream target:** none. This stays in the `zephyr-devel` module as a
  prototype playground. No Zephyr UART driver changes.

**Implementation plan:**
`docs/superpowers/plans/2026-08-25-uart-sbus-converter.md`

---

## 1. Purpose

S.BUS (Futaba / FrSky) is inverted UART, TX-only, 100000 bit/s, 8 data bits,
even parity, 2 stop bits. Frames are typically 25 bytes about every 10 ms.
This sample is the critical real-time RC path for a drone: a companion
computer (or other MCU) sends a byte stream on ordinary 115200 8N1 UART; the
Nucleo emits the same bytes on an S.BUS electrical/line-coding interface.

Latency is the first priority. The converter is a **transparent cut-through
byte pipe**: S.BUS TX starts on the first input byte, then a small queue
decouples RX fill from TX drain. It does not parse S.BUS frames.

### Goals

1. Receive 115200 baud, 8N1 on one USART.
2. Emit the same bytes on another USART as S.BUS: 100000 baud, 8E2,
   hardware TX inversion, TX-only.
3. Keep the ST-Link serial console (LPUART1) working.
4. Hard real-time, interrupt-based, low inter-byte jitter on S.BUS.
5. Start TX as soon as the first RX byte arrives; never wait for a 25-byte
   frame.
6. Count bytes and errors with atomics; print stats on the console every
   ~10 seconds, including estimated S.BUS frames/s for that interval.

### Non-goals

- S.BUS header/footer checks, channel packing, or CRSF/ELRS conversion.
- DMA / UART async API on the converter path.
- Failsafe frames, hold-last, or a locked 10 ms TX timer.
- External inverter hardware (STM32 `tx-invert` is required).
- Changes to in-tree Zephyr UART drivers or `nucleo_g431kb` board files.
- Running the full converter on `native_sim` (no inverted 8E2 UART).
- A hand-rolled ring buffer; the queue uses Zephyr
  [`spsc_lockfree`](https://docs.zephyrproject.org/latest/doxygen/html/group__spsc__lockfree.html).

---

## 2. Physical layer and timing

| Side | Baud | Frame | Invert | Direction | Nucleo-32 pin (UM2397) |
|---|---|---|---|---|---|
| Console (unchanged) | 115200 | 8N1 | no | LPUART1 TX/RX | PA2 / PA3 (ST-Link VCP only, **not** Arduino D0/D1) |
| Input | 115200 | 8N1 | no | USART1 RX | PA10 (Arduino **D0**, CN4 pin 2) |
| S.BUS output | 100000 | 8E2 | yes (`tx-invert`) | USART2 TX | PB3 (Arduino **D13**, CN3 pin 15) |

Arduino D0/D1 on this board are USART1 (PA10/PA9), not the VCP. D7 is
PF0 (OSC_IN) and D3 is PB0; neither can be a USART. Default solder bridge
SB2 routes PB7 to A4, so `usart1_rx_pb7` is not Arduino D4/D7.

Bit times:

- Input byte: 10 bits × 1/115200 ≈ **86.81 µs**
- S.BUS byte: 12 bits × 1/100000 = **120 µs**
- 25-byte burst: RX ≈ 2.17 ms, TX ≈ 3.00 ms

RX is faster in bytes/s than TX. During a 25-byte burst the software queue
grows by a few bytes, then drains in the inter-frame gap. A 64-byte queue
is enough for that pattern and leaves headroom.

S.BUS idle is UART idle inverted: line low when not transmitting. STM32
USART `tx-invert` provides that. No external inverter.

USART1 TX (PA9 / D1) is unused. USART2 RX is not muxed. Default board
I2C2 on PA8/PA9 is left as-is (PA9 is unused USART1 TX; no conflict with
PA10/PB3).

---

## 3. Architecture

Three units, one job each:

```text
USART1 RX IRQ  -->  sbus_pipe (SPSC queue)  -->  USART2 TX IRQ
                         | atomics
                         v
              main thread, printk every 10 s
                    (LPUART1 console)
```

1. **`sbus_pipe`** — thin wrapper around Zephyr `spsc_lockfree` (64-byte
   SPSC) plus the stats atomics. RX ISR is the only producer, TX ISR the
   only consumer (Zephyr’s supported ISR+ISR pairing). Unit-tested on
   `native_sim`.
2. **IRQ glue (`main`)** — Zephyr interrupt UART API: `uart_fifo_read` /
   `uart_fifo_fill`, TX IRQ enable/disable. Never parses bytes.
3. **Devicetree overlay** — line coding, inversion, FIFO, pinmux, aliases.

The main thread is not in the control path. Console I/O must not delay
USART1 or USART2 ISRs (NVIC priorities, below).

---

## 4. `sbus_pipe` interface

Capacity is **64** bytes (power of two, required by `SPSC_DEFINE` /
`SPSC_INITIALIZER`). One producer (RX ISR), one consumer (TX ISR). Do not
produce from two contexts or consume from two contexts.

The ring is Zephyr `spsc_lockfree` (`<zephyr/sys/spsc_lockfree.h>`).
`sbus_pipe` does not implement its own `head`/`tail` mask. `spsc_acquire()`
returning `NULL` is the full condition and **is** drop-newest.

```c
#define SBUS_PIPE_CAP 64

SPSC_DECLARE(byte, uint8_t);

struct sbus_pipe {
	struct spsc_byte q;
	uint8_t buf[SBUS_PIPE_CAP];
	atomic_t rx_bytes;   /* bytes accepted from UART RX */
	atomic_t tx_bytes;   /* bytes handed to UART TX */
	atomic_t rx_drops;   /* newest bytes dropped (queue full) */
	atomic_t rx_err;     /* HW RX errors reported by glue */
};

void sbus_pipe_init(struct sbus_pipe *p);

/* Producer: push one byte. Returns true if stored, false if dropped. */
bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b);

/* Consumer: pop one byte. Returns true if `*b` was set, false if empty. */
bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b);

bool sbus_pipe_is_empty(const struct sbus_pipe *p);
```

`sbus_pipe_init` must use `SPSC_INITIALIZER(SBUS_PIPE_CAP, p->buf)` (or
equivalent) so `q.buffer` points at `p->buf`. Do not `memset` the struct
to zero: `buffer` is a `const` pointer. `spsc_reset()` is allowed only
from init and from ztest `before()` — never from an ISR.

Push: `spsc_acquire(&p->q)`; if `NULL`, increment `rx_drops` and return
false; else write the byte, `spsc_produce(&p->q)`, increment `rx_bytes`.
Pop: `spsc_consume(&p->q)`; if `NULL`, return false; else copy the byte,
`spsc_release(&p->q)`, increment `tx_bytes`. Empty: `spsc_peek(&p->q) ==
NULL`.

`rx_err` is incremented by IRQ glue (not by push/pop) when
`uart_err_check()` reports overrun, noise, framing, or parity.

Counters are `atomic_t` and wrap naturally. Snapshot them with
`atomic_get` from the main thread. Because USART1 preempts USART2, TX
cannot run during the RX ISR; producing each byte in `push` vs
`spsc_produce_all` at the end of the RX ISR does not change cut-through
latency. `push` still produces immediately so the public API is one byte
at a time.

---

## 5. IRQ glue and cut-through

Use Zephyr **interrupt-driven UART** only (`uart_irq_*` / `uart_fifo_*`).
Do not use `uart_poll_*` or UART async/DMA on USART1/USART2.

**RX ISR (USART1), after `uart_irq_update`:**

1. If `uart_irq_rx_ready()`, loop `uart_fifo_read(..., 1)` and
   `sbus_pipe_push` each byte.
2. After any successful push, call `uart_irq_tx_enable(sbus_out)`.
   Enabling an already-enabled TX IRQ is required (avoids the
   empty→disable vs push→enable race).
3. If `uart_err_check()` is non-zero, `atomic_inc(&pipe.rx_err)` and
   clear via the driver API as required.

**TX ISR (USART2), after `uart_irq_update`:**

1. While `uart_irq_tx_ready()`, `sbus_pipe_pop`; on success
   `uart_fifo_fill` that byte and the pop already counted `tx_bytes`.
2. On empty queue, `uart_irq_tx_disable(sbus_out)` and return.
   Hardware then finishes the last stop bits and sits at inverted idle.

Cut-through: the first byte of a burst enables TX before the rest of the
burst has arrived. Inter-byte S.BUS gaps must stay at the hardware 8E2
spacing whenever the software queue and USART TX FIFO are non-empty.
Enable STM32 USART **hardware FIFO** (`fifo-enable`) on USART1 and USART2
so a late IRQ does not insert extra idle between S.BUS bytes.

ISR body may only: `uart_irq_update`, rx/tx ready, `uart_fifo_read` /
`uart_fifo_fill`, `sbus_pipe_push` / `pop`, `uart_irq_tx_enable` /
`disable`, `uart_err_check`, `atomic_inc` on `rx_err`. No `printk`,
locks, allocations, or kernel sleeps.

---

## 6. Real-time constraints

NVIC priority (numerically lower = higher priority on Cortex-M). The SoC
nodes already use two-cell `interrupts = <irq priority>`. The overlay
**replaces** those properties so the IRQ numbers stay the same and the
priorities become:

| UART | IRQ | Priority |
|---|---|---|
| USART1 (input) | 37 | 0 |
| USART2 (S.BUS TX) | 38 | 1 |
| LPUART1 (console) | 91 | 2 |

USART1 must preempt USART2 so the queue keeps filling during TX. Console
must not preempt either converter USART.

`CONFIG_UART_INTERRUPT_DRIVEN=y`. UART async Kconfig stays off unless a
dependency forces it; the sample must not call async APIs.

No workqueue, timer, or thread feeds S.BUS. A `k_sleep(K_SECONDS(10))`
loop in `main` is the only kernel wait, and only for stats.

---

## 7. Stats console

Every ~10 seconds the main thread snapshots the four atomics and prints
one line on the existing UART console. Lifetime counters stay cumulative.
**Frames/s is only for the last interval**, from the `tx_bytes` delta:

```text
tx_delta = (uint32_t)tx_now - (uint32_t)tx_prev   /* wrap-safe */
fps      = tx_delta / 25 / 10                     /* integer, truncated */
```

The divisor `10` is the nominal sleep (`K_SECONDS(10)`), not a measured
uptime. This is an estimate: the pipe does not parse 25-byte frames, so a
partial burst or a non-25-byte sender still uses `/ 25`.

Example (100 frames/s of 25-byte bursts):

```text
sbus: rx=25000 tx=25000 drops=0 err=0 fps=100
```

No print-on-change, no ISR logging. If `printk` blocks on a full console
buffer, that stalls only the main thread.

---

## 8. Devicetree and Kconfig

Board overlay `samples/uart_sbus/boards/nucleo_g431kb.overlay`:

- Chosen / aliases: `uart-in = &usart1`, `sbus-out = &usart2`. Console
  `zephyr,console` / `zephyr,shell-uart` remain `&lpuart1`.
- `&usart1`: `pinctrl` RX `usart1_rx_pa10` (D0), `current-speed = <115200>`,
  `status = "okay"`, `fifo-enable`. Optional `bias-pull-up` on that pinctrl node.
- `&usart2`: `pinctrl` TX `usart2_tx_pb3` (D13) only, `current-speed = <100000>`,
  `parity = "even"`, `stop-bits = "2"`, `tx-invert`, `fifo-enable`,
  `status = "okay"`. Do not enable RX IRQ on USART2.
- NVIC: `&usart1 { interrupts = <37 0>; }`, `&usart2 { interrupts = <38 1>; }`,
  `&lpuart1 { interrupts = <91 2>; }` (IRQ numbers from the G4 SoC DT;
  priorities as in §6).

`prj.conf` enables serial, interrupt-driven UART, UART console, and
atomics if not already implied by the board defconfig. Do not steal
LPUART1 from the console.

---

## 9. File layout

All paths under the `zephyr-devel` module root (this repo):

| Path | Role |
|---|---|
| `samples/uart_sbus/CMakeLists.txt` | Zephyr app |
| `samples/uart_sbus/prj.conf` | Sample Kconfig |
| `samples/uart_sbus/sample.yaml` | Twister: `nucleo_g431kb`, build-only |
| `samples/uart_sbus/README.rst` | Pins, line coding, build/flash, stats |
| `samples/uart_sbus/boards/nucleo_g431kb.overlay` | USART1/2, invert, FIFO |
| `samples/uart_sbus/src/sbus_pipe.h` | Pipe API (SPSC wrapper + counters) |
| `samples/uart_sbus/src/sbus_pipe.c` | `spsc_acquire`/`produce`/`consume`/`release` + atomics |
| `samples/uart_sbus/src/main.c` | IRQ glue + 10 s stats |
| `tests/sbus_pipe/` | ztest of the queue on `native_sim` |
| `zephyr/module.yml` | Register `samples:` and `tests:` |

Share `sbus_pipe` with the test by compiling the same `.c` from
`samples/uart_sbus/src/` (relative `target_sources` path). Do not
duplicate the queue. Do not re-test Zephyr SPSC itself; tests cover the
wrapper (drop-newest, counters, wrap of the 64-slot ring).

---

## 10. Testing

**Queue (required in CI):** `tests/sbus_pipe` on `native_sim` with ztest:

- empty pop fails
- push then pop returns the same byte
- fill to 64, next push fails and `rx_drops` increments; queued bytes
  unchanged (drop newest)
- wrap-around: push/pop across the modulo boundary
- `rx_bytes` / `tx_bytes` match successful push/pop counts

**Sample (CI):** twister build of `samples/uart_sbus` for `nucleo_g431kb`
only (`build_only: true`, `platform_allow: nucleo_g431kb`). No console
harness wait.

**Hardware (manual):** 115200 8N1 into D0 (PA10); logic analyzer or S.BUS device
on D13 (PB3) shows inverted 100 kbit/s 8E2, first output byte starts after the
first input byte (not after 25 bytes). Console line `sbus: ... fps=...`
every ~10 s. ST-Link VCP still serves as console.

---

## 11. Safety notes (transparent pipe)

A dropped byte desynchronizes any S.BUS consumer until the sender and
receiver happen to realign. That is accepted: this sample does not resync
on `0x0F` / 25-byte length / footer `0x00`.

If the input stream is sustained faster than S.BUS (continuous 115200
with no gaps), `rx_drops` grows and the output is a lossy prefix of the
input. Intended traffic is short bursts (≈25 bytes) with ≥ several
milliseconds of idle.

Loss of input: TX IRQ disables when the queue empties. The S.BUS line
goes idle. No failsafe frame is generated; a downstream flight controller
must apply its own failsafe on signal loss.
