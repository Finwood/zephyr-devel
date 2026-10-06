# sbus_bridge hardware-in-the-loop (Nucleo-G431KB)

ZTest fixture that black-box tests a production `samples/uart_sbus` image on
`starcopter/sbus_bridge`. One firmware image runs an EOL suite at boot
(cut-through, 100 Hz paced stream, gapless overload). Re-run by rebooting the
tester (shell or NRST).

Common ground between DUT and tester. Both run at 3.3 V.

## Wiring

![Nucleo-32 G431KB HIL pinout. UART out on D6. S.BUS in on D12. Console is ST-Link USB.](img/wiring.svg)

| DUT (`sbus_bridge`) | Direction | Tester |
| --- | --- | --- |
| PA1 — USART1 RX, 115200 8N1 | tester → DUT | PB6 — USART1 TX (D6) |
| PA4 — USART2 TX, inverted 100000 8E2 | DUT → tester | PB4 — USART2 RX, `rx-invert` (D12) |

DUT PA0 (USART1 TX) stays open. Common GND. 3.3 V.

On the tester, jumper **D6↔A0** and **D12↔A1** so TIM2 can input-capture the
same edges (CH1 falling on UART start, CH2 rising on inverted S.BUS start).

External status LEDs (open-drain active-low, same style as the uart_sbus
error LED):

| LED | Tester | Wiring |
| --- | --- | --- |
| Green (PASS) | D11 (PB5) | +5 V → ~330 Ω → LED → D11 |
| Red (FAIL) | D10 (PA11) | +5 V → ~330 Ω → LED → D10 |

Both off while a suite run is in progress; teardown latches green or red.

Tester console is LPUART1 (PA2/PA3) via the Nucleo’s ST-Link VCP.

Round-trip capture is used on the cut-through frame and on each paced frame.
It is **not** measured during the gapless case (overlapping start bits).

## Flash

From the `zephyr-devel` west workspace root, with `ZEPHYR_BASE` pointing at
`deps/zephyr`:

**DUT** — production image (flash once before powering the tester):

```bash
uv run west build -b sbus_bridge -d /tmp/b_sbus_bridge samples/uart_sbus
uv run west flash -d /tmp/b_sbus_bridge
```

Use the default classic footer (`CONFIG_UART_SBUS_SBUS2` off).

**Tester** — this directory:

```bash
uv run west build -b nucleo_g431kb -d /tmp/b_sbus_hil tests/sbus_bridge_hil
uv run west flash -d /tmp/b_sbus_hil
```

## Console

Open the tester’s ST-Link serial port at **115200 8N1**. On boot, ZTest runs
suite `eol` automatically and prints `PROJECT EXECUTION SUCCESSFUL` or
`PROJECT EXECUTION FAILED`. With fail-fast, remaining cases are skipped after
the first failure.

To re-run the suite, reboot the tester (resets ZTest and re-drains the DUT):

```text
kernel reboot
```

(`kernel reboot cold` / `kernel reboot warm` also work; or press NRST.)
Status LEDs latch green/red from suite teardown on each boot.
