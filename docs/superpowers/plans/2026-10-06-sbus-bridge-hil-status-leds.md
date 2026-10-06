# HIL status LEDs Implementation Plan

> **For agentic workers:** Implement from `docs/superpowers/specs/2026-10-06-sbus-bridge-hil-status-leds-design.md`.

**Goal:** Latch external green/red LEDs from ZTest suite setup/teardown on the HIL tester.

**Architecture:** DT `gpio-leds` on D11/D10 (OD active-low); helpers; `eol_setup` clears LEDs + records fail-count baseline; `eol_teardown` latches pass/fail. No custom `main`.

## Task 1: DT + config + LED helpers

- Overlay: green PB5/D11, red PA11/D10 under `&leds`; aliases `led-green` / `led-red`
- `prj.conf`: `CONFIG_GPIO=y`
- `src/status_led.c` / `.h`: off / pass / fail using `gpio_dt_spec` (same style as uart_sbus)
- Wire into CMakeLists

## Task 2: Suite hooks + docs

- Fixture with fail-count baseline via `z_ztest_get_next_test("eol", …)`
- `eol_setup` / `eol_teardown`; register teardown on `ZTEST_SUITE`
- README + wiring.svg callouts for D10/D11
- Build `nucleo_g431kb` HIL image
