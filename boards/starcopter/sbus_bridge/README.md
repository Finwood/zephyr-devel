<!--
SPDX-License-Identifier: MIT
-->

# starcopter UART S.BUS Bridge (`sbus_bridge`)

UART (115200 8N1) to inverted S.BUS (100 kbit/s 8E2) converter on
**STM32C031F6P6** (TSSOP-20, 32 KB Flash, 12 KB RAM, Cortex-M0+).

Both USARTs are used by the converter, so there is **no console UART**.
Console is **SEGGER RTT** over SWD.

## Pin map

| Net | Pin | Function |
| --- | --- | --- |
| UART TX | PA0 | USART1_TX, 115200 8N1 |
| UART RX | PA1 | USART1_RX, 115200 8N1 |
| S.BUS out | PA4 | USART2_TX inverted, 100000 8E2 |
| Activity LED | PA6 | GPIO, active-high |
| Error LED | PA7 | GPIO, active-high |
| SWDIO | PA13 | SWD |
| SWCLK | PA14 | SWD |
| NRST | PF2 | hardware reset |

SYSCLK is HSI 48 MHz (no HSE crystal). USART1 and USART2 share NVIC
priority 0 (required by `samples/uart_sbus`).

## Build and flash

RTT console needs the Zephyr `segger` module. From the `zephyr-devel`
workspace root, after `west update` has fetched `deps/modules/debug/segger`:

```
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus
uv run west flash -d /tmp/b_sbus_bridge
```

Default `west flash` uses **OpenOCD** (bundled with the Zephyr SDK) and an
ST-Link probe. Alternatives:

```
uv run west flash -d /tmp/b_sbus_bridge --runner stm32cubeprogrammer
uv run west flash -d /tmp/b_sbus_bridge --runner pyocd
```

pyOCD needs the Keil `STM32C0xx_DFP` CMSIS-Pack installed so the target
`stm32c031f6px` is available (`pyocd pack install STM32C0xx`).

If west does not see this module’s `boards/` (for example a git worktree
whose `deps/` symlink makes CMake discover another workspace), add:

```
uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus -- \
  -DBOARD_ROOT=$PWD -DZEPHYR_EXTRA_MODULES=$PWD
```

Stats `printk` goes to RTT, not UART.
