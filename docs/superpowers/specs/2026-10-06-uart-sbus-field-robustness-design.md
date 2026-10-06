# uart_sbus field robustness — Design

- **Date:** 2026-10-06
- **Status:** Approved
- **Scope:** Minimal field recovery/hardening for `samples/uart_sbus`,
  especially on `sbus_bridge` (STM32C031F6P6, 32 KB flash / 12 KB RAM).

## 1. Purpose

Keep the UART→S.BUS bridge running in the field when the MCU hangs or
corrupts a stack, without burning much flash or RAM on the C031.

### Goals

1. Recover from hard hangs / stuck main via independent IWDG reset (~50 ms).
2. Detect stack overflow via stack sentinel; fatal path reboots the SoC.
3. Keep the field image small: no MPU guards, canaries, asserts, or analyzer.

### Non-goals

- Hardware MPU stack guards (`CONFIG_HW_STACK_PROTECTION`).
- Stack canaries / entropy.
- `CONFIG_ASSERT` in the field build.
- Window watchdog (WWDG) or per-thread task WDT.
- Lab-only stack high-water tooling (separate conf left for later if needed).

## 2. Mechanisms

| Mechanism | Covers | Behavior |
| --- | --- | --- |
| STM32 IWDG | Hang / stuck main | 50 ms window; `wdt_feed` each main loop wake (≤5 ms) |
| `CONFIG_RESET_ON_FATAL_ERROR` | Fatal faults | Reboot after short dump instead of halt |
| `CONFIG_STACK_SENTINEL` | Stack overflow | Magic at stack bottom; corruption → fatal → reboot |
| `CONFIG_FAULT_DUMP=1` | Flash | Terse fault info (Zephyr default is 2) |
| Tuned stacks | RAM | `ISR_STACK_SIZE=1024`, `MAIN_STACK_SIZE=1024` |

## 3. Board / DT

- `sbus_bridge`: enable `&iwdg`, alias `watchdog0 = &iwdg`.
- `nucleo_g431kb`: board already aliases `watchdog0`; sample overlay sets
  `&iwdg { status = "okay"; }`.

## 4. Application

[`samples/uart_sbus/src/main.c`](../../../samples/uart_sbus/src/main.c)
installs a single IWDG channel (`window.max = 50`, no callback) after
UART/LED ready checks, then feeds it every main-loop iteration after
`k_sem_take`. Setup failure prints and returns like other device failures.

## 5. Kconfig

[`samples/uart_sbus/prj.conf`](../../../samples/uart_sbus/prj.conf):

```
CONFIG_WATCHDOG=y
CONFIG_RESET_ON_FATAL_ERROR=y
CONFIG_STACK_SENTINEL=y
CONFIG_FAULT_DUMP=1
CONFIG_ISR_STACK_SIZE=1024
CONFIG_MAIN_STACK_SIZE=1024
```

`nucleo_g431kb` board defconfig selects `CONFIG_HW_STACK_PROTECTION`, which
enables `MPU_STACK_GUARD` and mutually excludes `STACK_SENTINEL`. The sample
board conf sets `CONFIG_HW_STACK_PROTECTION=n` so both targets use the same
sentinel path.
## 6. Verification

- Build `sbus_bridge` and `nucleo_g431kb`; confirm C031 flash fit.
- Optional on hardware: omit feed and confirm ~50 ms reset.
