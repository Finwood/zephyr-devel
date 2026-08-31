# UART to S.BUS Converter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `zephyr-devel` sample that cut-through converts 115200 8N1 UART bytes to inverted 100 kbit/s 8E2 S.BUS on `nucleo_g431kb`, with a unit-tested lock-free byte pipe.

**Architecture:** RX ISR pushes bytes into `sbus_pipe` (Zephyr `spsc_lockfree` 64-byte queue, drop-newest, plus stats atomics) and enables S.BUS TX; TX ISR pops into the USART FIFO; main thread only prints 10 s stats. Line coding, `tx-invert`, FIFO, pins, and NVIC live in a board overlay.

**Tech Stack:** Zephyr interrupt UART API, [`spsc_lockfree`](https://docs.zephyrproject.org/latest/doxygen/html/group__spsc__lockfree.html), STM32 USART `tx-invert` + `fifo-enable`, ztest on `native_sim`, `uv run west` / twister.

**Spec:** `docs/superpowers/specs/2026-08-25-uart-sbus-converter-design.md`

## Global Constraints

- **Repo:** Edit `zephyr-devel` only. Do not change `deps/zephyr` UART drivers or `nucleo_g431kb` board files.
- **Branch:** Stay on the existing `cursor/`-prefixed branch (example `cursor/uart-sbus-converter`; Cloud suffix OK).
- **Board:** `nucleo_g431kb` (STM32G431KB Nucleo-32).
- **Console:** Keep ST-Link VCP on LPUART1 (PA2/PA3). Do not steal it.
- **Aliases:** `uart-in = &usart1`, `sbus-out = &usart2` (not `sbus-in`).
- **Input:** USART1 RX PB7 (D7), 115200 8N1, not inverted.
- **Output:** USART2 TX PB3 (D3), 100000 baud, 8E2, `tx-invert`, TX-only.
- **Path:** Transparent cut-through byte pipe. No S.BUS parse, no CRSF, no DMA/async UART, no failsafe frames, no external inverter.
- **Overflow:** Drop newest (`spsc_acquire` returns `NULL`); increment `rx_drops`; leave queued bytes unchanged.
- **Queue:** Wrap Zephyr `<zephyr/sys/spsc_lockfree.h>`. Do not implement a custom ring (`head`/`tail` mask). RX ISR is the only producer; TX ISR is the only consumer. `spsc_reset` only from init / ztest `before()`.
- **Stats:** Every `K_SECONDS(10)` print `sbus: rx=%u tx=%u drops=%u err=%u fps=%u` with wrap-safe `fps = tx_delta / 25 / 10`.
- **ISRs:** Only `uart_irq_*` / `uart_fifo_*` / `sbus_pipe_push`/`pop` / `uart_err_check` / `atomic_inc` on `rx_err`. No `printk` from ISRs.
- **NVIC:** USART1 `<37 0>`, USART2 `<38 0>`, LPUART1 `<91 2>`.
- **Build:** `ZEPHYR_BASE=<repo>/deps/zephyr`, prefix west with `uv run`.
- **Commits:** Conventional Commits. Do not use `git commit -s` / `--signoff`. Do not commit secrets.

---

## File map

| Path | Role |
|---|---|
| `samples/uart_sbus/src/sbus_pipe.h` | Pipe API (`SPSC_DECLARE` + counters) |
| `samples/uart_sbus/src/sbus_pipe.c` | `spsc_acquire`/`produce`/`consume`/`release` + atomics |
| `tests/sbus_pipe/src/main.c` | ztest of the same `.c` |
| `tests/sbus_pipe/CMakeLists.txt` | Test app; compiles sample `sbus_pipe.c` |
| `tests/sbus_pipe/prj.conf` | `CONFIG_ZTEST=y` |
| `tests/sbus_pipe/tests.yaml` | `native_sim` |
| `samples/uart_sbus/src/main.c` | IRQ glue + 10 s stats |
| `samples/uart_sbus/CMakeLists.txt` | Sample app |
| `samples/uart_sbus/prj.conf` | Interrupt UART + console |
| `samples/uart_sbus/sample.yaml` | `nucleo_g431kb`, `build_only` |
| `samples/uart_sbus/README.rst` | Pins, build, stats, safety |
| `samples/uart_sbus/boards/nucleo_g431kb.overlay` | USART1/2, invert, FIFO, NVIC |
| `zephyr/module.yml` | Register `samples` and `tests` |

---

### Task 1: `sbus_pipe` + native_sim ztest

**Files:**
- Create: `samples/uart_sbus/src/sbus_pipe.h`
- Create: `samples/uart_sbus/src/sbus_pipe.c`
- Create: `tests/sbus_pipe/src/main.c`
- Create: `tests/sbus_pipe/CMakeLists.txt`
- Create: `tests/sbus_pipe/prj.conf`
- Create: `tests/sbus_pipe/tests.yaml`
- Modify: `zephyr/module.yml`

**Interfaces:**
- Consumes: Zephyr `<zephyr/sys/spsc_lockfree.h>` (`spsc_acquire` / `spsc_produce` / `spsc_consume` / `spsc_release` / `spsc_peek`); `atomic_inc` / `atomic_get` / `atomic_set`
- Produces:
  - `#define SBUS_PIPE_CAP 64`
  - `SPSC_DECLARE(byte, uint8_t)` and `struct sbus_pipe` with `struct spsc_byte q`, `uint8_t buf[64]`, `rx_bytes`, `tx_bytes`, `rx_drops`, `rx_err`
  - `void sbus_pipe_init(struct sbus_pipe *p)` — `SPSC_INITIALIZER(SBUS_PIPE_CAP, p->buf)` plus zeroed counters
  - `bool sbus_pipe_push(struct sbus_pipe *p, uint8_t b)` — true if stored; on full increment `rx_drops` and return false; increment `rx_bytes` only on success
  - `bool sbus_pipe_pop(struct sbus_pipe *p, uint8_t *b)` — true if `*b` set; increment `tx_bytes` on success
  - `bool sbus_pipe_is_empty(const struct sbus_pipe *p)`

- [ ] **Step 1: Register tests in the module and add the failing test app**

Replace `zephyr/module.yml` with:

```yaml
name: zephyr-devel
tests:
  - tests
```

Create `tests/sbus_pipe/prj.conf`:

```
CONFIG_ZTEST=y
```

Create `tests/sbus_pipe/tests.yaml`:

```yaml
tests:
  sbus.pipe:
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
    tags:
      - sbus
```

Create `tests/sbus_pipe/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sbus_pipe)

target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/uart_sbus/src/sbus_pipe.c
)
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/uart_sbus/src
)
```

Create `tests/sbus_pipe/src/main.c`:

```c
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
```

- [ ] **Step 2: Run the test and confirm it fails**

From the `zephyr-devel` repo root:

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
uv run west twister -T tests/sbus_pipe -p native_sim -v
```

Expected: FAIL (CMake/compile cannot find `samples/uart_sbus/src/sbus_pipe.c` or `sbus_pipe.h`).

- [ ] **Step 3: Implement `sbus_pipe`**

Create `samples/uart_sbus/src/sbus_pipe.h`:

```c
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
```

Create `samples/uart_sbus/src/sbus_pipe.c`:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sbus_pipe.h"

void sbus_pipe_init(struct sbus_pipe *p)
{
	p->q = (struct spsc_byte)SPSC_INITIALIZER(SBUS_PIPE_CAP, p->buf);
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
```

Do not `memset` / `(struct sbus_pipe){0}` the whole object: `q.buffer` is `uint8_t * const` and must stay pointed at `p->buf`. Do not increment `rx_err` here; IRQ glue owns that counter. Do not call `spsc_reset` from ISRs.

- [ ] **Step 4: Run the tests and confirm they pass**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
uv run west twister -T tests/sbus_pipe -p native_sim -v
```

Expected: `sbus.pipe` PASS on `native_sim` (all four tests).

- [ ] **Step 5: Commit**

```bash
git add zephyr/module.yml \
  samples/uart_sbus/src/sbus_pipe.h samples/uart_sbus/src/sbus_pipe.c \
  tests/sbus_pipe/
git commit -m "$(cat <<'EOF'
feat(sbus): wrap Zephyr SPSC in UART/S.BUS byte pipe

EOF
)"
```

---

### Task 2: Sample IRQ glue, overlay, stats, docs

**Files:**
- Create: `samples/uart_sbus/src/main.c`
- Create: `samples/uart_sbus/CMakeLists.txt`
- Create: `samples/uart_sbus/prj.conf`
- Create: `samples/uart_sbus/sample.yaml`
- Create: `samples/uart_sbus/README.rst`
- Create: `samples/uart_sbus/boards/nucleo_g431kb.overlay`
- Modify: `zephyr/module.yml`

**Interfaces:**
- Consumes: `sbus_pipe_*` from Task 1; `DT_ALIAS(uart_in)`, `DT_ALIAS(sbus_out)`
- Produces: running sample on `nucleo_g431kb`; console stats line; twister build-only case `sample.uart_sbus`

- [ ] **Step 1: Overlay, Kconfig, CMake, twister metadata**

Set `zephyr/module.yml` to:

```yaml
name: zephyr-devel
samples:
  - samples
tests:
  - tests
```

Create `samples/uart_sbus/boards/nucleo_g431kb.overlay`:

```dts
/*
 * SPDX-License-Identifier: Apache-2.0
 */

/ {
	aliases {
		uart-in = &usart1;
		sbus-out = &usart2;
	};
};

&usart1 {
	pinctrl-0 = <&usart1_rx_pb7>;
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
	interrupts = <38 0>;
	status = "okay";
};

&lpuart1 {
	interrupts = <91 2>;
};
```

Create `samples/uart_sbus/prj.conf`:

```
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

Do not set `CONFIG_UART_ASYNC_API`.

Create `samples/uart_sbus/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uart_sbus)

target_sources(app PRIVATE src/main.c src/sbus_pipe.c)
```

Create `samples/uart_sbus/sample.yaml`:

```yaml
sample:
  name: UART to S.BUS converter
tests:
  sample.uart_sbus:
    platform_allow:
      - nucleo_g431kb
    integration_platforms:
      - nucleo_g431kb
    build_only: true
    tags:
      - uart
      - sbus
```

Create `samples/uart_sbus/src/main.c`:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "sbus_pipe.h"

#define UART_IN_NODE  DT_ALIAS(uart_in)
#define SBUS_OUT_NODE DT_ALIAS(sbus_out)

BUILD_ASSERT(DT_NODE_EXISTS(UART_IN_NODE), "alias uart-in missing");
BUILD_ASSERT(DT_NODE_EXISTS(SBUS_OUT_NODE), "alias sbus-out missing");

static const struct device *const uart_in = DEVICE_DT_GET(UART_IN_NODE);
static const struct device *const sbus_out = DEVICE_DT_GET(SBUS_OUT_NODE);

static struct sbus_pipe pipe;

static void uart_in_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	int err;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	err = uart_err_check(dev);
	if (err != 0) {
		atomic_inc(&pipe.rx_err);
	}

	while (uart_irq_rx_ready(dev) > 0) {
		if (uart_fifo_read(dev, &c, 1) < 1) {
			break;
		}
		if (sbus_pipe_push(&pipe, c)) {
			uart_irq_tx_enable(sbus_out);
		}
	}
}

static void sbus_out_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	while (uart_irq_tx_ready(dev) > 0) {
		if (!sbus_pipe_pop(&pipe, &c)) {
			uart_irq_tx_disable(dev);
			return;
		}
		if (uart_fifo_fill(dev, &c, 1) < 1) {
			uart_irq_tx_disable(dev);
			return;
		}
	}
}

int main(void)
{
	uint32_t tx_prev = 0;

	sbus_pipe_init(&pipe);

	if (!device_is_ready(uart_in) || !device_is_ready(sbus_out)) {
		printk("sbus: UART device not ready\n");
		return 0;
	}

	uart_irq_callback_user_data_set(uart_in, uart_in_cb, NULL);
	uart_irq_callback_user_data_set(sbus_out, sbus_out_cb, NULL);
	uart_irq_err_enable(uart_in);
	uart_irq_rx_enable(uart_in);

	printk("sbus: uart-in -> sbus-out cut-through\n");

	for (;;) {
		uint32_t rx;
		uint32_t tx;
		uint32_t drops;
		uint32_t err;
		uint32_t tx_delta;
		uint32_t fps;

		k_sleep(K_SECONDS(10));

		rx = (uint32_t)atomic_get(&pipe.rx_bytes);
		tx = (uint32_t)atomic_get(&pipe.tx_bytes);
		drops = (uint32_t)atomic_get(&pipe.rx_drops);
		err = (uint32_t)atomic_get(&pipe.rx_err);
		tx_delta = tx - tx_prev;
		fps = tx_delta / 25U / 10U;
		tx_prev = tx;

		printk("sbus: rx=%u tx=%u drops=%u err=%u fps=%u\n",
		       rx, tx, drops, err, fps);
	}
}
```

Do not call `uart_irq_rx_enable(sbus_out)`. Do not use `uart_poll_*` or UART async APIs. After every successful push, call `uart_irq_tx_enable(sbus_out)` even if TX IRQ is already enabled.

Create `samples/uart_sbus/README.rst`:

```rst
UART to S.BUS converter
#######################

Cut-through converter: bytes arriving on ordinary 115200 8N1 UART are
emitted on an inverted 100 kbit/s 8E2 S.BUS UART. The sample does not
parse S.BUS frames. Transmission starts on the first input byte.

Supported board: ``nucleo_g431kb``.

Wiring (Nucleo-32 Arduino Nano header)
**************************************

- Console (ST-Link VCP): LPUART1 PA2/PA3 (D1/D0), 115200 8N1
- Input: USART1 RX **PB7 (D7)**, 115200 8N1
- S.BUS output: USART2 TX **PB3 (D3)**, 100000 8E2, hardware ``tx-invert``,
  TX-only. Idle is low. No external inverter.

Building and flashing
*********************

.. code-block:: console

   export ZEPHYR_BASE=$PWD/deps/zephyr
   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
   uv run west flash -d /tmp/b_uart_sbus

Stats
*****

Every 10 seconds the console prints lifetime counters and an estimated
frames/s for that interval (``tx_delta / 25 / 10``):

.. code-block:: console

   sbus: rx=25000 tx=25000 drops=0 err=0 fps=100

A dropped byte can desynchronize an S.BUS consumer. If input stops, S.BUS
goes idle; the flight controller must apply its own failsafe.
```

- [ ] **Step 2: Build the sample for `nucleo_g431kb`**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
```

Expected: configure + link succeed. If pinctrl TX-only fails the STM32 driver, keep TX-only first; do not add a dummy RX pin unless the build error proves it is required, then mux no extra header pin (do not steal PA2/PA3).

- [ ] **Step 3: Twister build-only**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
uv run west twister -T samples/uart_sbus -p nucleo_g431kb --build-only
```

Expected: `sample.uart_sbus` PASS (built).

- [ ] **Step 4: Re-run pipe tests (no regression)**

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
uv run west twister -T tests/sbus_pipe -p native_sim
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add zephyr/module.yml samples/uart_sbus/
git commit -m "$(cat <<'EOF'
feat(sbus): add UART to S.BUS cut-through sample

EOF
)"
```

Hardware check (manual, not a gate): 115200 8N1 into D7; inverted 100 kbit/s 8E2 on D3 starting after the first input byte; ST-Link console still works; stats line every ~10 s.

---

## Spec coverage

| Spec section | Task |
|---|---|
| §1 Goals 1–6, non-goals | Task 2 (pipe in Task 1) |
| §2 Pins / 8E2 / invert | Task 2 overlay + README |
| §3 Architecture | Task 1 pipe, Task 2 ISRs |
| §4 `sbus_pipe` wraps `spsc_lockfree` + drop-newest | Task 1 |
| §5 Cut-through IRQ glue, FIFO, no poll/async | Task 2 |
| §6 NVIC 37/38/91 priorities 0/1/2 | Task 2 overlay |
| §7 Stats + interval `fps` | Task 2 `main.c` |
| §8 DT aliases `uart-in` / `sbus-out` | Task 2 overlay |
| §9 File layout + shared `.c` | Task 1 CMake include path; Task 2 sample |
| §10 ztest + build-only twister | Task 1 / Task 2 |
| §11 Transparent-pipe safety notes | Task 2 README |
