UART to S.BUS converter
#######################

Cut-through converter: classic 25-byte S.BUS frames arriving on ordinary
115200 8N1 UART are emitted on an inverted 100 kbit/s 8E2 S.BUS UART.
Transmission starts on the first header byte (``0x0F``), not after the
footer. A locked 25-byte window follows each header; payload ``0x0F``
does not resync. A complete frame still waiting to start TX is dropped
as a whole when a newer valid frame is committed.

Pipeline internals (slots, cut-through, supersede): `PIPELINE.md <PIPELINE.md>`_.

Supported board: ``nucleo_g431kb``.

Wiring (Nucleo-32 Arduino Nano header)
**************************************

.. figure:: img/wiring.svg
   :align: center
   :alt: Nucleo-32 G431KB with USB at top. UART in on D0. S.BUS out on D13.
         Red error LED on D12 to 5 V. Console is ST-Link USB, not D0/D1.

   Nucleo-32 pinout (USB / ST-LINK at the top) per ST UM2397. D0/D1 are USART1,
   not the ST-Link VCP. VCP is LPUART1 on PA2/PA3, wired only to the debugger.

- Console: ST-Link VCP (USB serial), LPUART1 PA2/PA3, 115200 8N1. Not Arduino D0/D1.
- Input: USART1 RX **PA10 (Arduino D0, CN4 pin 2)**, 115200 8N1
- S.BUS output: USART2 TX **PB3 (Arduino D13, CN3 pin 15)**, 100000 8E2,
  hardware ``tx-invert``, TX-only. Idle is low. No external inverter.
- Green activity: onboard **LD2 (PB8)**, 50 ms pulse every 10 transmitted frames
- Red error: external LED on **D12 / PB4 (CN4 pin 15)**, GPIO open-drain
  active-low. Wire **+5 V (CN3 pin 4)** → ~330 Ω → LED anode → cathode to D12.
  200 ms on supersede, 2 s on UART/sync faults (not sticky; retrigger uses max
  remaining vs new duration).
- Tie companion UART, Nucleo, and S.BUS device **GND** together.
- Do not use D7 or D3: D7 is PF0 (OSC_IN) and D3 is PB0. Neither is a USART pin.
- Do not use A7 for the LED. A7 is PA2 (LPUART1 TX / ST-Link VCP) and is not
  5 V-tolerant.

Building and flashing
*********************

.. code-block:: console

   export ZEPHYR_BASE=$PWD/deps/zephyr
   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
   uv run west flash -d /tmp/b_uart_sbus --runner openocd

Optional Futaba S.BUS2 slot footers (``0x04`` / ``0x14`` / ``0x24`` / ``0x34``)::

   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus -- -DCONFIG_UART_SBUS_SBUS2=y

Default is classic S.BUS only (footer ``0x00``). Inter-window telemetry bytes
are always dropped while hunting; they do not increment ``sync``.

Stats
*****

Every 1 second the console prints lifetime counters and frames/s for that
interval (``tx_frames`` delta / elapsed whole seconds):

.. code-block:: console

   sbus: rx=25000 tx=25000 err=0 frames=1000 fps=100 sup=0 sync=0

``frames`` is lifetime fully transmitted valid S.BUS frames. ``sup`` is
waiting frames dropped as stale. ``sync`` is bad footers at the end of a
25-byte window. A bad footer after cut-through still emits the bytes already
on the wire; the incomplete ``current`` is drained and promoted. If input
stops, S.BUS goes idle; the flight controller must apply its own failsafe.
