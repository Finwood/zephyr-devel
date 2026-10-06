# sbus_bridge Board Definition — Design

- **Date:** 2026-09-30
- **Status:** Implemented (build-verified; no HIL)
- **Scope:** Out-of-tree Zephyr board `sbus_bridge` for the ordered starcopter
  UART-to-S.BUS board (STM32C031F6P6, TSSOP-20).
- **Upstream target:** none. The board stays in this module under
  `boards/starcopter/sbus_bridge/`. It replaces the `sbus_c031g6` prototype.

## 1. Purpose

`boards/starcopter/sbus_bridge/sbus_bridge.dts` already describes the ordered
board. The rest of a hardware-model v2 board definition is missing, and the
USART1 pinmux names pins that do not exist on this package.

This work makes `west build -b sbus_bridge` and `west flash` work. OpenOCD is
the default runner. STM32CubeProgrammer and pyOCD are available with
`--runner`.

### Goals

1. Keep the existing DTS, and point USART1 at the pins the silicon actually
   muxes.
2. Add the board metadata, Kconfig, defconfig, twister YAML, runners, and a
   short README, following `boards/finwood/sbus_c031g6`.
3. Default console is SEGGER RTT. Both USARTs belong to the converter.
4. Program over SWD (PA13, PA14) with hardware reset on PF2-NRST.

### Non-goals

- `doc/index.rst`, a picture, or a vendor entry in Zephyr
  `vendor-prefixes.txt`.
- A schematic or BOM write-up. The schematic already exists.
- Upstream Zephyr.

### Sample board fragment (required for a clean build)

`samples/uart_sbus` enables the UART console on Nucleo via
`boards/nucleo_g431kb.conf`. The bridge has no spare USART, so
`boards/sbus_bridge.conf` enables RTT instead. `sample.yaml` lists
`sbus_bridge` in `platform_allow` / `integration_platforms`.

## 2. Hardware the files must match

| Item | Value |
| --- | --- |
| Board target | `sbus_bridge` |
| SoC | `stm32c031xx` |
| Part | STM32C031F6P6, TSSOP-20, 32 KB Flash, 12 KB RAM |
| Vendor string | `starcopter` (all lowercase, including the human-readable name) |
| Human-readable name | `starcopter UART S.BUS Bridge` |
| SYSCLK | HSI 48 MHz, already in the DTS |
| USART1 | TX PA0 (`usart1_tx_pa0`, AF4), RX PA1 (`usart1_rx_pa1`, AF4), 115200 8N1 |
| USART2 | TX PA4, 100000 8E2, `tx-invert`, already in the DTS |
| LEDs | PA6 activity, PA7 error, both active-high, already in the DTS |
| Debug | SWDIO PA13, SWCLK PA14, NRST PF2 |

`usart1_rx_pa0` and `usart1_tx_pa1` are not in
`stm32c031f(4-6)px-pinctrl.dtsi`. PA0 is USART1_TX and PA1 is USART1_RX.

## 3. Files

All files live in `boards/starcopter/sbus_bridge/` and use the MIT license.
The DTS header stays as it is. Every other file starts with
`SPDX-License-Identifier: MIT` in that file's comment syntax: `#` for CMake,
Kconfig, YAML, and OpenOCD Tcl; an HTML comment for `README.md`. No
Apache-2.0 headers. The snippets below are the file body; the SPDX line
comes first.

### 3.1 `sbus_bridge.dts`

One change: USART1 `pinctrl-0` becomes `<&usart1_tx_pa0 &usart1_rx_pa1>`.
Nothing else in the DTS changes.

### 3.2 `board.yml`

```yaml
board:
  name: sbus_bridge
  full_name: starcopter UART S.BUS Bridge
  vendor: starcopter
  socs:
    - name: stm32c031xx
```

### 3.3 `Kconfig.sbus_bridge`

```kconfig
config BOARD_SBUS_BRIDGE
	select SOC_STM32C031XX
```

No `Kconfig` or `Kconfig.defconfig`. The prototype board has neither.

### 3.4 `sbus_bridge_defconfig`

Same options as `sbus_c031g6_defconfig`:

- `CONFIG_SERIAL=y`
- `CONFIG_UART_INTERRUPT_DRIVEN=y`
- `CONFIG_GPIO=y`
- `CONFIG_CONSOLE=y`
- `CONFIG_UART_CONSOLE=n`
- `CONFIG_RTT_CONSOLE=y`
- `CONFIG_USE_SEGGER_RTT=y`

Comment: both USARTs are used by the converter, so the console is RTT over SWD.

### 3.5 `sbus_bridge.yaml`

```yaml
identifier: sbus_bridge
name: starcopter UART S.BUS Bridge
type: mcu
arch: arm
toolchain:
  - zephyr
  - gnuarmemb
supported:
  - gpio
  - uart
ram: 12
flash: 32
vendor: starcopter
```

### 3.6 `board.cmake`

Include order sets the default runner. OpenOCD is first.

- `stm32cubeprogrammer`: `--port=swd` `--reset-mode=hw`
- `pyocd`: `--target=stm32c031f6px`
- Includes, in order: `openocd.board.cmake`, `stm32cubeprogrammer.board.cmake`,
  `pyocd.board.cmake`

OpenOCD finds `support/openocd.cfg` on its own. pyOCD finds
`support/pyocd.yaml` for `flash`, `reset`, and `rtt`. The Zephyr pyOCD runner
does not pass that file to `gdbserver`; `west debug` still gets the target
from `--target`.

### 3.7 `support/openocd.cfg`

Source `interface/stlink.cfg`, select `hla_swd`, source `target/stm32c0x.cfg`,
then force NRST:

```
reset_config srst_nogate
cortex_m reset_config srst
```

`stm32c0x.cfg` selects `sysresetreq` when the session is not HLA. The lines
above run after that script so PF2-NRST is the reset that `west flash` uses.
The probe assumed here is an ST-Link, matching how `sbus_c031g6` is flashed.
A different probe needs a different `interface/` script.

### 3.8 `support/pyocd.yaml`

```yaml
reset_type: hw
```

pyOCD has no built-in STM32C031 target. `stm32c031f6px` comes from the
Keil `STM32C0xx_DFP` CMSIS-Pack. The README says to install that pack before
`west flash --runner pyocd`.

### 3.9 `README.md`

Short. It records:

- Part, package, Flash, RAM, and the lowercase name.
- Pin map: PA0 USART1 TX, PA1 USART1 RX, PA4 USART2 S.BUS TX, PA6 activity
  LED, PA7 error LED, PA13 SWDIO, PA14 SWCLK, PF2 NRST.
- RTT console, and why there is no console UART.
- Build: `uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus`
  from the workspace root, with the same `ZEPHYR_BASE` / SDK exports as the
  `sbus_c031g6` README.
- Flash: default OpenOCD; `--runner stm32cubeprogrammer`; `--runner pyocd`
  after the CMSIS-Pack install.

## 4. Verification

From the `zephyr-devel` workspace root, with the usual Zephyr SDK environment:

1. `uv run west boards` lists `sbus_bridge`.
2. `uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus`
   succeeds. Twister will not build this board for that sample, because
   `platform_allow` is unchanged on purpose.
3. The generated devicetree uses `usart1_tx_pa0` and `usart1_rx_pa1`.
4. `west flash` is not run. No board is attached.

## 5. Failure notes

- OpenOCD is wired for an ST-Link. Another probe fails until
  `support/openocd.cfg` sources that probe's interface script.
- pyOCD fails with an unknown target until `STM32C0xx_DFP` is installed.
- The build proves the board files. It does not prove SWD, NRST, or the LED
  wiring.
