# sbus_bridge HIL Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Nucleo-G431KB ZTest fixture that black-box tests a production `uart_sbus` image on `sbus_bridge`, with a boot-time EOL suite (cut-through, 100 Hz paced, gapless) and TIM2 input-capture round-trip measurement.

**Architecture:** Tester-only app under `tests/sbus_bridge_hil/`. USART1 TX feeds the DUT UART; USART2 RX (`rx-invert`) captures inverted S.BUS. TIM2 CH1/CH2 timestamp start bits on jumpered nets. `frame` encodes/checks payload CRC; `wire` drives/collects UART bytes; `capture` arms single-shot TIM2 edges; `main` runs the three cases and an `eol` shell re-run.

**Tech Stack:** Zephyr ZTest, interrupt UART, STM32 counter capture (`CONFIG_COUNTER_CAPTURE`), CRC software (`crc16_itu_t`), shell (not `CONFIG_ZTEST_SHELL`), `uv run west` on `nucleo_g431kb`.

**Spec:** `docs/superpowers/specs/2026-10-05-sbus-bridge-hil-design.md`

## Global Constraints

- **Repo:** Edit `zephyr-devel` only. Do not change `samples/uart_sbus`, `boards/starcopter/sbus_bridge`, or `deps/zephyr`.
- **Branch:** Prefer `cursor/sbus-bridge-hil` (Cloud may append a suffix). Stay on a `cursor/`-prefixed branch.
- **DUT:** Production image: `west build -b sbus_bridge samples/uart_sbus` with default classic footer (`CONFIG_UART_SBUS_SBUS2` off). HIL never flashes the DUT.
- **Tester board:** `nucleo_g431kb` only. Console stays LPUART1 PA2/PA3. Do not remux those pins.
- **Pins:** USART1 TX PB6 (D5); USART2 RX PB4 (D12) with `rx-invert`, 100000 8E2; TIM2 CH1 PA0 (A0) falling; TIM2 CH2 PA1 (A1) rising. Local jumpers PB6↔PA0 and PB4↔PA1.
- **TIM2:** 10 MHz counter clock. On this board SYSCLK is 170 MHz with APB1 = 1, so `st,prescaler = <16>` (170/10 − 1).
- **Frame layout:** byte0 `0x0F`; 1–4 seq LE; 5–8 send-time µs LE; 9–20 `0x5A`; 21–22 CRC LE; 23 `0x00`; 24 `0x00`. CRC = `crc16_itu_t(0xFFFF, &buf[1], 20)`.
- **Oracle:** Valid frames only. Sequence gap ≥ 1 and ≤ 2. Last counter sent must appear. Corrupt frames fail the case. Gapless capture stays off.
- **Boot suite order:** cut-through → paced (100 @ 100 Hz) → gapless (100 back-to-back). `CONFIG_ZTEST_FAIL_FAST=y`.
- **Shell:** `CONFIG_SHELL=y`, `CONFIG_ZTEST_SHELL=n`. Boot runs via `test_main`. Command `eol` re-runs suite and prints `eol: PASS` or `eol: FAIL` (ZTest's `PROJECT EXECUTION …` line only appears once at boot).
- **License:** Apache-2.0 SPDX on new C/CMake/overlay/conf files (match `tests/sbus_pipe`).
- **Build:** Export `ZEPHYR_BASE=<repo>/deps/zephyr`. Prefix west with `uv run`.
- **Commits:** Conventional Commits. Do not use `--signoff`. Do not commit secrets.
- **Hardware:** Full case pass/fail needs boards wired. Until then, verify with `west build` only and note HIL not run.

---

## File map

| Path | Role |
| --- | --- |
| `tests/sbus_bridge_hil/CMakeLists.txt` | App build |
| `tests/sbus_bridge_hil/prj.conf` | ZTest, shell, UART IRQ, CRC, counter capture, fail-fast |
| `tests/sbus_bridge_hil/tests.yaml` | `nucleo_g431kb` only, harness `ztest` |
| `tests/sbus_bridge_hil/boards/nucleo_g431kb.overlay` | USART1/2 + TIM2 capture pinmux |
| `tests/sbus_bridge_hil/src/frame.h` / `frame.c` | Encode/check 25-byte test frames |
| `tests/sbus_bridge_hil/src/wire.h` / `wire.c` | USART1 TX + USART2 RX collector |
| `tests/sbus_bridge_hil/src/capture.h` / `capture.c` | TIM2 single-shot RTT |
| `tests/sbus_bridge_hil/src/main.c` | Suite + shell `eol` |
| `tests/sbus_bridge_hil/README.md` | Wiring + flash commands |

---

### Task 1: Scaffold + frame encode/check

**Files:**
- Create: `tests/sbus_bridge_hil/CMakeLists.txt`
- Create: `tests/sbus_bridge_hil/prj.conf`
- Create: `tests/sbus_bridge_hil/tests.yaml`
- Create: `tests/sbus_bridge_hil/boards/nucleo_g431kb.overlay` (minimal stubs; full pinmux in Task 2)
- Create: `tests/sbus_bridge_hil/src/frame.h`
- Create: `tests/sbus_bridge_hil/src/frame.c`
- Create: `tests/sbus_bridge_hil/src/main.c` (CRC known-vector case only)

**Interfaces:**
- Consumes: `<zephyr/sys/crc.h>` (`crc16_itu_t`), `<zephyr/sys/byteorder.h>`
- Produces:
  - `#define HIL_FRAME_LEN 25`, `HIL_HDR 0x0F`, `HIL_FTR 0x00`, `HIL_FILL 0x5A`
  - `void hil_frame_encode(uint8_t out[HIL_FRAME_LEN], uint32_t seq, uint32_t t_us)`
  - `bool hil_frame_check(const uint8_t in[HIL_FRAME_LEN], uint32_t *seq, uint32_t *t_us)` — `true` only if header, flags, footer, and CRC match; writes `*seq` and `*t_us` on success

- [ ] **Step 1: Create build skeleton**

```cmake
# tests/sbus_bridge_hil/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sbus_bridge_hil)

target_sources(app PRIVATE
  src/main.c
  src/frame.c
)
```

```conf
# tests/sbus_bridge_hil/prj.conf
CONFIG_ZTEST=y
CONFIG_ZTEST_FAIL_FAST=y
CONFIG_SHELL=y
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CRC=y
CONFIG_COUNTER=y
CONFIG_COUNTER_CAPTURE=y
```

```yaml
# tests/sbus_bridge_hil/tests.yaml
common:
  platform_allow:
    - nucleo_g431kb
  tags:
    - sbus
    - hil
  harness: ztest
tests:
  sbus.bridge.hil: {}
```

Stub overlay (empty body is fine until Task 2):

```dts
/*
 * SPDX-License-Identifier: Apache-2.0
 */

 / {
 };
```

- [ ] **Step 2: Implement `frame.h` / `frame.c`**

```c
/* frame.h */
#ifndef SBUS_BRIDGE_HIL_FRAME_H_
#define SBUS_BRIDGE_HIL_FRAME_H_

#include <stdbool.h>
#include <stdint.h>

#define HIL_FRAME_LEN 25
#define HIL_HDR       0x0F
#define HIL_FTR       0x00
#define HIL_FILL      0x5A

void hil_frame_encode(uint8_t out[HIL_FRAME_LEN], uint32_t seq, uint32_t t_us);
bool hil_frame_check(const uint8_t in[HIL_FRAME_LEN], uint32_t *seq, uint32_t *t_us);

#endif
```

```c
/* frame.c — encode fills bytes 0..24; check validates and extracts seq/t_us */
#include "frame.h"

#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

void hil_frame_encode(uint8_t out[HIL_FRAME_LEN], uint32_t seq, uint32_t t_us)
{
	uint16_t crc;

	out[0] = HIL_HDR;
	sys_put_le32(seq, &out[1]);
	sys_put_le32(t_us, &out[5]);
	memset(&out[9], HIL_FILL, 12);
	crc = crc16_itu_t(0xFFFF, &out[1], 20);
	sys_put_le16(crc, &out[21]);
	out[23] = 0x00;
	out[24] = HIL_FTR;
}

bool hil_frame_check(const uint8_t in[HIL_FRAME_LEN], uint32_t *seq, uint32_t *t_us)
{
	uint16_t expect;

	if (in[0] != HIL_HDR || in[23] != 0x00 || in[24] != HIL_FTR) {
		return false;
	}
	expect = crc16_itu_t(0xFFFF, &in[1], 20);
	if (sys_get_le16(&in[21]) != expect) {
		return false;
	}
	*seq = sys_get_le32(&in[1]);
	*t_us = sys_get_le32(&in[5]);
	return true;
}
```

- [ ] **Step 3: Add known-vector ZTest case in `main.c`**

Known vector (seq=0, t_us=`0x01020304`):

```
0f 00 00 00 00 04 03 02 01 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 04 2b 00 00
```

```c
#include <zephyr/ztest.h>
#include "frame.h"

ZTEST(eol, test_frame_crc_vector)
{
	uint8_t frame[HIL_FRAME_LEN];
	uint32_t seq;
	uint32_t t_us;
	static const uint8_t expect[HIL_FRAME_LEN] = {
		0x0f, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
		0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
		0x5a, 0x5a, 0x5a, 0x04, 0x2b, 0x00, 0x00,
	};

	hil_frame_encode(frame, 0, 0x01020304);
	zassert_mem_equal(frame, expect, HIL_FRAME_LEN);
	zassert_true(hil_frame_check(frame, &seq, &t_us));
	zassert_equal(seq, 0);
	zassert_equal(t_us, 0x01020304);
	frame[21] ^= 0x01;
	zassert_false(hil_frame_check(frame, &seq, &t_us));
}

ZTEST_SUITE(eol, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 4: Build for nucleo_g431kb**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil --pristine
```

Expected: configure + link succeed. Flash is optional here; the CRC case needs no DUT.

- [ ] **Step 5: Commit**

```bash
git add tests/sbus_bridge_hil
git commit -m "$(cat <<'EOF'
test(sbus): scaffold sbus_bridge HIL and frame CRC

Add the nucleo_g431kb HIL app skeleton and a known-vector check for
the 25-byte seq/timestamp/CRC payload layout.

EOF
)"
```

---

### Task 2: Devicetree overlay + wire TX/RX

**Files:**
- Modify: `tests/sbus_bridge_hil/boards/nucleo_g431kb.overlay`
- Create: `tests/sbus_bridge_hil/src/wire.h`
- Create: `tests/sbus_bridge_hil/src/wire.c`
- Modify: `tests/sbus_bridge_hil/CMakeLists.txt` (add `src/wire.c`)
- Modify: `tests/sbus_bridge_hil/src/main.c` (aliases / smoke init only)

**Interfaces:**
- Consumes: DT aliases `uart-out` (USART1), `sbus-in` (USART2); `hil_frame_*`
- Produces:
  - `int hil_wire_init(void)`
  - `void hil_wire_reset(void)` — clear RX log, error counters, assembler state
  - `int hil_wire_send_frame(const uint8_t frame[HIL_FRAME_LEN])` — block until all 25 bytes accepted by USART1
  - `int hil_wire_send_frame_at(const uint8_t frame[HIL_FRAME_LEN], uint32_t t_us)` — like send, but encode time is already in `frame`
  - `size_t hil_wire_rx_count(void)`
  - `bool hil_wire_rx_get(size_t i, uint32_t *seq, uint32_t *t_us)`
  - `uint32_t hil_wire_corrupt_count(void)` — bad CRC / framing / parity / unexpected header
  - `bool hil_wire_idle_since(int64_t since_ms, int32_t window_ms)` — true if no RX byte in `[since_ms, since_ms+window_ms]`
  - `int64_t hil_wire_last_rx_ms(void)`

- [ ] **Step 1: Write the full overlay**

```dts
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wiring (with DUT sbus_bridge):
 *   PB6 (D5)  USART1 TX 115200 8N1  -> DUT PA1
 *   PB4 (D12) USART2 RX 100k 8E2 rx-invert <- DUT PA4
 *   PA0 (A0)  TIM2_CH1 jumpered to PB6 (UART start, falling)
 *   PA1 (A1)  TIM2_CH2 jumpered to PB4 (S.BUS start, rising)
 * Console LPUART1 PA2/PA3 untouched.
 */

/ {
	aliases {
		uart-out = &usart1;
		sbus-in = &usart2;
	};
};

&usart1 {
	pinctrl-0 = <&usart1_tx_pb6>;
	pinctrl-names = "default";
	current-speed = <115200>;
	fifo-enable;
	interrupts = <37 1>;
	status = "okay";
};

&usart2 {
	pinctrl-0 = <&usart2_rx_pb4>;
	pinctrl-names = "default";
	current-speed = <100000>;
	parity = "even";
	stop-bits = "2";
	rx-invert;
	fifo-enable;
	interrupts = <38 1>;
	status = "okay";
};

&timers2 {
	st,prescaler = <16>;
	status = "okay";

	counter2: counter {
		status = "okay";
		pinctrl-0 = <&tim2_ch1_pa0 &tim2_ch2_pa1>;
		pinctrl-names = "default";
	};
};
```

Notes: USART IRQ priority 1 (below nothing critical; keep equal between USARTs). TIM2 PSC 16 → 10 MHz at 170 MHz timer clock. Counter node label `counter2` for `DEVICE_DT_GET(DT_NODELABEL(counter2))` in Task 3.

- [ ] **Step 2: Implement `wire` TX/RX**

Keep RX log capacity ≥ 100 frames:

```c
#define HIL_RX_LOG_MAX 128
```

TX: IRQ callback drains a 25-byte buffer via `uart_fifo_fill`; give a `k_sem` when emptied. RX: IRQ reads bytes, hunts `0x0F`, collects 25, calls `hil_frame_check`; on success append `{seq,t_us}`; on failure increment `corrupt`. Also increment `corrupt` on `uart_err_check` parity/framing. Record `k_uptime_get()` on every RX byte for idle checks.

`hil_wire_send_frame`: copy frame, enable TX IRQ, take sem with timeout (e.g. 50 ms).

- [ ] **Step 3: Rebuild**

```bash
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
```

Expected: build succeeds with USART + TIM2 nodes.

- [ ] **Step 4: Commit**

```bash
git add tests/sbus_bridge_hil
git commit -m "$(cat <<'EOF'
test(sbus): add HIL UART wire path and pinmux

Map Nucleo USART1/USART2 and TIM2 capture pins, and collect valid
S.BUS frames from the inverted RX path.

EOF
)"
```

---

### Task 3: TIM2 capture helper

**Files:**
- Create: `tests/sbus_bridge_hil/src/capture.h`
- Create: `tests/sbus_bridge_hil/src/capture.c`
- Modify: `tests/sbus_bridge_hil/CMakeLists.txt`

**Interfaces:**
- Consumes: `DEVICE_DT_GET(DT_NODELABEL(counter2))`, counter capture API
- Produces:
  - `int hil_capture_init(void)` — `counter_start`, configure CH0 falling + CH1 rising, both `COUNTER_CAPTURE_SINGLE_SHOT`
  - `int hil_capture_arm(void)` — enable both channels; clear prior results
  - `int hil_capture_wait(uint32_t *t_uart, uint32_t *t_sbus, k_timeout_t timeout)` — wait until both callbacks fired; return `-EAGAIN` on timeout, `-EIO` on overcapture flag if exposed
  - `uint32_t hil_capture_ticks_to_us(uint32_t ticks)` — `counter_ticks_to_us`
  - `void hil_capture_disarm(void)` — disable both channels

Channel IDs: TIM2 CH1 → `0`, TIM2 CH2 → `1` (see Zephyr `tests/drivers/counter/counter_capture`).

- [ ] **Step 1: Implement capture.c**

Pattern (from Zephyr capture tests):

```c
ret = counter_capture_configure(dev, 0,
	COUNTER_CAPTURE_FALLING_EDGE | COUNTER_CAPTURE_SINGLE_SHOT,
	uart_cb, NULL);
ret = counter_capture_configure(dev, 1,
	COUNTER_CAPTURE_RISING_EDGE | COUNTER_CAPTURE_SINGLE_SHOT,
	sbus_cb, NULL);
counter_start(dev);
```

Each callback stores ticks and gives a sem (or counts to 2). `hil_capture_arm` re-enables after single-shot auto-disable.

- [ ] **Step 2: Rebuild**

```bash
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
```

- [ ] **Step 3: Commit**

```bash
git add tests/sbus_bridge_hil/src/capture.c tests/sbus_bridge_hil/src/capture.h tests/sbus_bridge_hil/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(sbus): add TIM2 start-bit capture for HIL RTT

Timestamp UART and inverted S.BUS start bits on a shared 10 MHz
counter for round-trip measurement.

EOF
)"
```

---

### Task 4: Cut-through + paced + gapless cases

**Files:**
- Modify: `tests/sbus_bridge_hil/src/main.c`

**Interfaces:**
- Consumes: `hil_frame_*`, `hil_wire_*`, `hil_capture_*`
- Produces: `ZTEST(eol, test_cut_through)`, `ZTEST(eol, test_paced)`, `ZTEST(eol, test_gapless)` plus shared helpers

Constants (match the spec):

```c
#define HIL_STREAM_N           100
#define HIL_PACED_PERIOD_MS    10
#define HIL_PACED_SEND_GAP_MIN_US  8000
#define HIL_PACED_SEND_GAP_MAX_US 12000
#define HIL_RTT_MIN_US         50
#define HIL_RTT_MAX_US         500
#define HIL_GAPLESS_RX_MIN     65
#define HIL_GAPLESS_RX_MAX     85
#define HIL_GAPLESS_SEND_GAP_MIN_US 1500
#define HIL_GAPLESS_SEND_GAP_MAX_US 6000
#define HIL_UART_BYTE_US       87   /* 10/115200 ≈ 86.8 */
#define HIL_FRAME_UART_US      (HIL_FRAME_LEN * HIL_UART_BYTE_US)
#define HIL_IDLE_MS            20
#define HIL_SETTLE_MS          5
#define HIL_FIRST_FRAME_TO_MS  50
```

Shared helpers in `main.c`:

```c
static void settle(void); /* wait until no RX for HIL_SETTLE_MS */
static void assert_stream(const char *name, uint32_t sent_last,
			  size_t rx_min, size_t rx_max,
			  uint32_t seq_gap_max,
			  uint32_t send_gap_min_us, uint32_t send_gap_max_us,
			  bool check_idle);
```

`assert_stream` must:
1. Fail if `hil_wire_corrupt_count() != 0`
2. Fail if `rx_count` outside `[rx_min, rx_max]`
3. Walk RX log: gaps in `[1, seq_gap_max]`; track min/max seq gap and send-time gap; require last seq `== sent_last`
4. `TC_PRINT` the four extrema
5. If `check_idle`: after last RX, wait `HIL_IDLE_MS` and fail if any new byte arrived

- [ ] **Step 1: `test_cut_through`**

```c
ZTEST(eol, test_cut_through)
{
	uint8_t frame[HIL_FRAME_LEN];
	uint32_t t_uart, t_sbus, seq, t_us;
	uint32_t rtt_us;

	settle();
	hil_wire_reset();
	zassert_ok(hil_capture_arm());

	hil_frame_encode(frame, 0, k_cyc_to_us_near32(k_cycle_get_32()));
	/* Arm before send so UART falling edge is not missed */
	zassert_ok(hil_wire_send_frame(frame));

	zassert_ok(hil_capture_wait(&t_uart, &t_sbus, K_MSEC(20)));
	/* Cut-through: S.BUS start before UART footer would finish */
	zassert_true((t_sbus - t_uart) <
		     counter_us_to_ticks(capture_dev, HIL_FRAME_UART_US));

	/* Wait for the full valid frame out */
	zassert_true(WAIT_FOR(hil_wire_rx_count() >= 1, 50000, k_msleep(1)));
	zassert_equal(hil_wire_rx_count(), 1);
	zassert_equal(hil_wire_corrupt_count(), 0);
	zassert_true(hil_wire_rx_get(0, &seq, &t_us));
	zassert_equal(seq, 0);

	rtt_us = hil_capture_ticks_to_us(t_sbus - t_uart);
	TC_PRINT("cut-through rtt=%u us\n", rtt_us);
	hil_capture_disarm();
}
```

Use the real capture device pointer from `capture.c` (export a getter or the ticks helper that subtracts safely with wrap). Prefer unsigned modular subtract for 32-bit TIM2: `(t_sbus - t_uart)`.

- [ ] **Step 2: `test_paced`**

For `i` in `0..99`:
1. `hil_capture_arm()`
2. Encode with `seq=i`, `t_us` = current µs
3. `hil_wire_send_frame`
4. `hil_capture_wait` within 20 ms; accumulate min/max RTT
5. Sleep until `start_ms + (i+1)*10` before next submit

After loop: require first valid frame within 50 ms of first send (fail early if `rx_count==0` at that point — implement by checking after frame 0 wait). Then `assert_stream(..., 99, 100, 100, 1, 8000, 12000, true)`. Assert every RTT in 50–500 µs; print min/max RTT.

- [ ] **Step 3: `test_gapless`**

`hil_capture_disarm()` for the whole case. Send 100 frames back-to-back (`hil_wire_send_frame` returns when USART accepted the 25 bytes; immediately encode/send next). Then `assert_stream(..., 99, 65, 85, 2, 1500, 6000, true)`.

- [ ] **Step 4: Keep `test_frame_crc_vector` first or drop it from `eol`**

Keep it as the first case in suite `eol` (fast, no hardware). Order in file: crc vector, cut-through, paced, gapless. Fail-fast still applies.

- [ ] **Step 5: Build**

```bash
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
```

If hardware is available:

```bash
uv run west flash -d /tmp/b_sbus_bridge   # DUT production image, once
uv run west flash -d /tmp/b_sbus_hil
# open ST-Link serial 115200; expect PROJECT EXECUTION SUCCESSFUL
```

- [ ] **Step 6: Commit**

```bash
git add tests/sbus_bridge_hil/src/main.c
git commit -m "$(cat <<'EOF'
test(sbus): add cut-through, paced, and gapless HIL cases

Boot the Nucleo EOL suite against a live sbus_bridge, including
round-trip capture on paced frames and overload drop bounds.

EOF
)"
```

---

### Task 5: Shell `eol` re-run + README

**Files:**
- Modify: `tests/sbus_bridge_hil/src/main.c`
- Create: `tests/sbus_bridge_hil/README.md`

**Interfaces:**
- Consumes: `ztest_run_test_suite`, shell API
- Produces: shell command `eol` → re-run suite → print `eol: PASS` or `eol: FAIL`

- [ ] **Step 1: Register shell command**

```c
#include <zephyr/shell/shell.h>

static int cmd_eol(const struct shell *sh, size_t argc, char **argv)
{
	int fail;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	fail = ztest_run_test_suite(eol, false, 1, 1, NULL);
	if (fail == 0) {
		shell_print(sh, "eol: PASS");
		return 0;
	}
	shell_print(sh, "eol: FAIL");
	return -EIO;
}

SHELL_CMD_REGISTER(eol, NULL, "Re-run sbus_bridge HIL suite", cmd_eol);
```

Do not enable `CONFIG_ZTEST_SHELL` (that skips boot `test_main`).

- [ ] **Step 2: Write README.md**

Include:
- Purpose (EOL + stress on one image)
- Wiring table from the spec (DUT ↔ Nucleo + jumpers)
- DUT flash commands
- Tester flash commands
- Console: 115200 on ST-Link USB; boot auto-runs; type `eol` to repeat
- Note that gapless RTT is not measured

- [ ] **Step 3: Rebuild + (optional) flash**

```bash
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
```

- [ ] **Step 4: Commit**

```bash
git add tests/sbus_bridge_hil
git commit -m "$(cat <<'EOF'
test(sbus): add HIL shell re-run and fixture README

Expose an eol shell command for factory retest and document the
Nucleo wiring and flash steps.

EOF
)"
```

---

## Self-review

1. **Spec coverage:** Purpose/non-goals → Global Constraints. Fixture pins + TIM2 → Task 2/3. Frame/oracle → Task 1 + Task 4 helpers. Boot suite cases → Task 4. Software layout → File map. How to run → Task 5 README. Shell re-run → Task 5 (`eol: PASS/FAIL` because `PROJECT EXECUTION` is boot-only).
2. **Placeholders:** None intentionally left. Hardware-dependent verification is called out as optional flash.
3. **Type consistency:** `hil_frame_*`, `hil_wire_*`, `hil_capture_*` names match across tasks. Suite name `eol` matches shell command and spec.
4. **Gap note:** Spec asked shell to “clear previous verdict”; ZTest does not re-print `PROJECT EXECUTION` without reboot, so the plan uses an explicit `eol: PASS`/`FAIL` line instead.
