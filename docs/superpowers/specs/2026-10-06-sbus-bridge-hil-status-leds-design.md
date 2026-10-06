# sbus_bridge HIL status LEDs — Design

- **Date:** 2026-10-06
- **Status:** Approved
- **Scope:** External red/green LEDs on the Nucleo-G431KB HIL tester that
  latch the last `eol` suite result. Lives under `tests/sbus_bridge_hil/`.

## 1. Purpose

Factory and lab operators need a glanceable pass/fail on the tester without
reading the serial console. Two external LEDs latch the result of each ZTest
suite run (boot-time suite and shell `eol` / `eol N`).

### Goals

1. Green on / red off after a passing suite; red on / green off after a
   failing suite; both off while a suite is running.
2. Drive LEDs only from the suite **setup** and **teardown** hooks (no
   custom `test_main`, no LED updates inside individual test bodies or the
   `eol` shell command beyond what the suite hooks already do).
3. Match the uart_sbus error-LED electrical style (open-drain active-low to
   +5 V).

### Non-goals

- Blink / “running” animations.
- Onboard LD2 as a status LED.
- DUT-side LEDs.
- Changing HIL UART, TIM2 capture, or DUT wiring.

## 2. Behavior

| Phase | Green (D11) | Red (D10) |
| --- | --- | --- |
| Suite setup (start of each suite run) | off | off |
| Suite teardown, all cases passed | on | off |
| Suite teardown, any case failed (incl. fail-fast) | off | on |

Each iteration of `eol N` is a full suite run, so setup clears the LEDs and
teardown re-latches after every iteration. The final latch is the last
completed iteration (or the failing one if the `eol` loop stops early).

## 3. Wiring

Same pattern as `samples/uart_sbus` error LED on Nucleo-G431KB:

- **+5 V** (CN3 pin 4) → ~330 Ω → LED anode
- LED cathode → GPIO (open-drain, active-low)
- Common GND with the rest of the HIL fixture

| LED | Arduino | MCU | Role |
| --- | --- | --- | --- |
| Green | D11 | PB5 | PASS |
| Red | D10 | PA11 | FAIL |

Pins stay clear of D6/D12 (UART/S.BUS), A0/A1 (TIM2 jumpers), and PA2/PA3
(VCP).

Document in `tests/sbus_bridge_hil/README.md` and `img/wiring.svg`.

## 4. Software

### Devicetree (`boards/nucleo_g431kb.overlay`)

Extend `&leds` with two `gpio-leds` nodes (open-drain, active-low), aliased
e.g. `led-green` / `led-red` (or `led0` / `led1` with clear labels).

### Kconfig

`CONFIG_GPIO=y`, `CONFIG_LED=y` (gpio-leds driver).

### Helper

Small `status_led.c` / `.h` (or inline in `main.c` if tiny):

- `hil_status_leds_off(void)` — both off
- `hil_status_leds_pass(void)` / `hil_status_leds_fail(void)` — mutual exclusion

Uses `gpio_dt_spec` / `gpio_pin_set_dt` on the DT LED nodes (same style as
`samples/uart_sbus`).

### ZTest hooks (only LED control points)

```text
ZTEST_SUITE(eol, NULL, eol_setup, NULL, NULL, eol_teardown);
```

- **`eol_setup`:** call `hil_status_leds_off()`, then existing wire/capture
  init. Record a baseline of per-case `fail_count` sums for suite `"eol"`
  (via `z_ztest_get_next_test`) so teardown can detect failures from *this*
  run only (stats are cumulative across `eol N` / re-runs).
- **`eol_teardown`:** if any case’s `fail_count` rose above the baseline →
  `hil_status_leds_fail()`; else `hil_status_leds_pass()`.

No custom `main` / `test_main`. Boot autorun and shell `eol` both go through
the same suite hooks.

### Shell `eol`

Unchanged aside from whatever suite hooks already do. Do not set LEDs in
`cmd_eol` itself.

## 5. Files touched

| Path | Change |
| --- | --- |
| `boards/nucleo_g431kb.overlay` | LED nodes + aliases; comment pins |
| `prj.conf` | `CONFIG_GPIO`, `CONFIG_LED` |
| `src/main.c` | setup/teardown LED logic (+ fail-count baseline) |
| optional `src/status_led.c/.h` | LED helpers if not kept in `main.c` |
| `CMakeLists.txt` | add source if split out |
| `README.md` | LED wiring table |
| `img/wiring.svg` | D10/D11 callouts |

## 6. Verification

1. Build/flash tester; with DUT healthy, boot suite → green on, red off.
2. Force a fail (e.g. disconnect S.BUS) → red on, green off after suite.
3. `eol` then `eol 3` with DUT healthy → LEDs off between iterations, green
   latched at the end.
4. Confirm UART/S.BUS/TIM2 HIL cases still pass with LEDs attached.
