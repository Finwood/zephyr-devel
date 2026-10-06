# SPDX-License-Identifier: MIT

board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(pyocd "--target=stm32c031f6px")

# keep first — default west flash / west debug runner
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
