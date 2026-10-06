# sbus_bridge Hardware-in-the-Loop Test — Design

- **Date:** 2026-10-05
- **Status:** Approved design, not implemented
- **Scope:** A ZTest application on a NUCLEO-G431KB that black-box tests a
  production `samples/uart_sbus` image running on `sbus_bridge`.
- **Upstream target:** none. The test stays in this module under
  `tests/sbus_bridge_hil/`.

## 1. Purpose

`tests/sbus_pipe` calls `sbus_pipe_push` and `sbus_pipe_pop` from one thread
on `native_sim`. It never sees USART FIFO timing, inverted S.BUS on the
wire, or an assembled board.

This test does two jobs with one image:

1. Measure timing, overload, and frame drops on real UARTs.
2. Give the factory line a boot-time pass/fail for every assembled
   `sbus_bridge` board.

The device under test runs the production build unchanged. Classic footer
`0x00` only (`CONFIG_UART_SBUS_SBUS2` off). The tester judges the board from
the bytes on the two serial wires and from timer captures of those wires.

### Goals

1. On every tester boot, run cut-through, a 100 Hz paced stream, and a
   gapless overload stream, then print one factory verdict.
2. Record sequence gaps and send-time gaps on every stream, and round-trip
   time on the cut-through frame and on every paced frame. Assert the bounds
   in this spec.
3. Leave a shell command that runs the same suite again.

### Non-goals

- Changes to `samples/uart_sbus` or to the `sbus_bridge` board files.
- Reading DUT counters over RTT or SWD. Watching the activity or error LEDs.
  Pulsing the DUT reset pin.
- An S.BUS2 DUT image. Bad-footer cases. Those stay in `tests/sbus_pipe`.
- A host-side oracle, pytest, or a twister run on `native_sim`.
- Per-frame round-trip capture during the gapless stream. Start bits overlap
  there, so capture stays disarmed.

## 2. Fixture

Both boards are 3.3 V. Common ground. The DUT cable is two signals:

| DUT pin | Direction | Tester pin |
| --- | --- | --- |
| PA1, USART1 RX, 115200 8N1 | tester → DUT | PB6, USART1 TX (Arduino D5) |
| PA4, USART2 TX, inverted 100000 8E2 | DUT → tester | PB4, USART2 RX with `rx-invert` (Arduino D12) |

DUT PA0 (USART1 TX) is left open. The production image does not drive it.

A pin cannot be a USART and a timer channel at once, so each net is also
jumpered to a TIM2 capture pin on the Nucleo. The jumpers stay on the tester.
TIM2 is free (the board uses LPTIM1 as the system timer and TIM4 for the LED
PWM). It is 32-bit. Zephyr's STM32 counter driver captures edges. Both
channels share one counter, clocked at 10 MHz (100 ns per tick).

| Tester pin | Edge | Net |
| --- | --- | --- |
| PA0 (Arduino A0), TIM2 CH1 | falling | UART TX, idle high. Start bit. |
| PA1 (Arduino A1), TIM2 CH2 | rising | Raw S.BUS, idle low. Inverted start bit. |

Round-trip time is the CH2 stamp minus the CH1 stamp. Capture is single-shot
and is armed only while both lines are idle: the cut-through frame, and each
frame of the paced run. A missed edge times out through `hil_capture_wait`
with `-EAGAIN`. The STM32 Zephyr counter driver does not surface CCxOF to its
callback, so overcapture cannot currently fail the case.

The console stays LPUART1 on PA2/PA3, the ST-Link USB serial port, 115200
8N1. The overlay must leave those pins alone. I2C2 on PA8/PA9 and TIM4 on PB8
stay as the board defines them.

## 3. Frame and oracle

The bridge forwards payload bytes unchanged. The tester fills them:

| Bytes | Contents |
| --- | --- |
| 0 | Header `0x0F` |
| 1–4 | Sequence, `uint32` little-endian, starting at 0 |
| 5–8 | Send time, `uint32` microseconds, tester clock, taken when byte 0 is submitted to USART1 |
| 9–20 | Fill `0x5A` |
| 21–22 | CRC, `uint16` little-endian |
| 23 | Flags `0x00` |
| 24 | Footer `0x00` |

The CRC is `crc16_itu_t(0xFFFF, bytes 1–20, 20)` from `<zephyr/sys/crc.h>`
(poly `0x1021`, seed `0xFFFF`, no reflection, no final XOR). That is
CRC-16/CCITT-FALSE.

A received frame is valid when the header, flags, footer, and CRC match.
USART2 parity and framing errors fail the case. A bad CRC fails the case. A
missing counter is a drop, not a corrupt frame.

For the valid frames, in arrival order:

- Sequence gap is `seq[i] - seq[i-1]`. Every gap is at least 1. Order is
  that assertion.
- Send-time gap is the difference of the embedded send times.
- The case prints the minimum and maximum of both gaps.

The largest sequence gap is at most 2 on every stream. The bridge holds one
frame on the wire and one complete frame waiting. A newer valid frame
replaces that waiting frame, which drops one counter. A second replacement
before the in-flight frame finishes would take about 4.3 ms of further UART
data after the waiting slot was emptied, and the in-flight S.BUS frame
releases its slot in 3.0 ms. Counters that arrive therefore never jump by
more than 2. A gap of 3 or more fails the board.

The receive count still has its own window. A gap of at most 2 allows
dropping every other frame, and the count catches that.

The paced and gapless cases, after the last frame they expect has arrived,
open a 20 ms quiet window and fail if any further byte arrives. Every case
starts only after both wires have been idle for 5 ms, so the DUT pipe is
hunting. The tester does not reset the DUT.

## 4. Boot suite

One ZTest suite, `eol`. Boot runs it. The shell command `eol` runs it again
and clears the previous verdict first, so a later pass prints a fresh
success line. The first failing case stops the suite
(`CONFIG_ZTEST_FAIL_FAST`).

Factory output is ZTest's own line: `PROJECT EXECUTION SUCCESSFUL` or
`PROJECT EXECUTION FAILED`. Each case prints its recorded gaps, and the
round-trip cases print microseconds, before that line.

| Order | Case | Stimulus | Assertions |
| --- | --- | --- | --- |
| 1 | Cut-through | One valid frame from idle | Exactly one valid frame, counter 0. CH2 stamp is strictly earlier than the UART header stamp plus 25 byte times at 115200 8N1 (10 bits per byte). The case prints that round-trip time in microseconds. |
| 2 | Paced | 100 frames. Byte 0 of frame N+1 is submitted 10 ms after byte 0 of frame N | 100 valid frames. Every sequence gap is 1. Counter 99 is present. Send-time gaps are in 8–12 ms. Each round-trip time is in 50–500 µs. The case prints the minimum and maximum. If no valid frame has arrived 50 ms after the first byte, the case fails and stops. Then the 20 ms idle check. |
| 3 | Gapless | 100 frames. The next frame is submitted as soon as USART1 has accepted the previous 25 bytes | 65–85 valid frames. Every sequence gap is 1 or 2. Counter 99 is present. Send-time gaps are in 1.0–6 ms. Capture stays disarmed. Then the 20 ms idle check. |

The paced period is 10 ms, which is slower than one 3.0 ms S.BUS frame, so
every frame fits. The gapless window is centered near 74 of 100: a UART frame
is 2.17 ms and an S.BUS frame is 3.0 ms, and the first frames get out before
the transmit FIFO is full. 65–85 covers clock error and still fails a silent
board or a board that forwards every frame. The 50 µs round-trip floor
rejects a short between the two nets. The 500 µs ceiling rejects a bridge
that waits for a full input frame before transmitting. One UART byte is about
87 µs, which is the physical floor of a real cut-through.

## 5. Tester software

All of it lives in `tests/sbus_bridge_hil/`. New files use the Apache-2.0
header, matching `tests/sbus_pipe`.

| File | Responsibility |
| --- | --- |
| `src/frame.c`, `src/frame.h` | Build a frame and check header, flags, footer, and CRC. No hardware. |
| `src/wire.c`, `src/wire.h` | Drive USART1. Collect USART2 bytes into valid frames. Count corrupt frames and parity errors. |
| `src/capture.c`, `src/capture.h` | Arm and read the two TIM2 single-shot captures. |
| `src/main.c` | The three cases and the `eol` shell command. |
| `boards/nucleo_g431kb.overlay` | USART1 on PB6, USART2 on PB4 (`rx-invert`, 100000, even parity, two stop bits), TIM2 CH1 on PA0, TIM2 CH2 on PA1. |
| `prj.conf` | ZTest, shell, interrupt-driven UART, counter, fail-fast. `CONFIG_ZTEST_SHELL` stays off so the suite runs from `test_main` at boot. |
| `tests.yaml` | `platform_allow: nucleo_g431kb` only, harness `ztest`. |
| `README.md` | Wiring table and the two flash commands below. |

`tests.yaml` does not list `native_sim`, so `west twister -p native_sim`
does not select this suite.

## 6. How to run

DUT, production image, flashed before the tester boots:

```
uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus
uv run west flash -d /tmp/b_sbus_bridge
```

Tester:

```
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
uv run west flash -d /tmp/b_sbus_hil
```

Open the ST-Link serial port at 115200 8N1. Boot runs the suite. Type `eol`
to run it again.
