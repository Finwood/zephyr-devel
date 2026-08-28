UART to S.BUS converter
#######################

Cut-through converter: bytes arriving on ordinary 115200 8N1 UART are
emitted on an inverted 100 kbit/s 8E2 S.BUS UART. The sample does not
parse S.BUS frames. Transmission starts on the first input byte.

Supported board: ``nucleo_g431kb``.

Wiring (Nucleo-32 Arduino Nano header)
**************************************

.. figure:: img/wiring.svg
   :align: center
   :alt: Nucleo-32 G431KB with USB at top. UART in on D0 (left header, 2nd pin).
         S.BUS out on D13 (right header, last pin). Console is ST-Link USB, not D0/D1.

   Nucleo-32 pinout (USB / ST-LINK at the top) per ST UM2397. D0/D1 are USART1,
   not the ST-Link VCP. VCP is LPUART1 on PA2/PA3, wired only to the debugger.

- Console: ST-Link VCP (USB serial), LPUART1 PA2/PA3, 115200 8N1. Not Arduino D0/D1.
- Input: USART1 RX **PA10 (Arduino D0, CN4 pin 2)**, 115200 8N1
- S.BUS output: USART2 TX **PB3 (Arduino D13, CN3 pin 15)**, 100000 8E2,
  hardware ``tx-invert``, TX-only. Idle is low. No external inverter.
- Tie companion UART, Nucleo, and S.BUS device **GND** together.
- Do not use D7 or D3: D7 is PF0 (OSC_IN) and D3 is PB0. Neither is a USART pin.

Building and flashing
*********************

.. code-block:: console

   export ZEPHYR_BASE=$PWD/deps/zephyr
   uv run west build -b nucleo_g431kb -d /tmp/b_uart_sbus samples/uart_sbus
   uv run west flash -d /tmp/b_uart_sbus --runner openocd

Stats
*****

Every 10 seconds the console prints lifetime counters and an estimated
frames/s for that interval (``tx_delta / 25 / 10``):

.. code-block:: console

   sbus: rx=25000 tx=25000 drops=0 err=0 fps=100

A dropped byte can desynchronize an S.BUS consumer. If input stops, S.BUS
goes idle; the flight controller must apply its own failsafe.
