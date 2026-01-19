# Copyright (c) 2025 AirSpot
# SPDX-License-Identifier: Apache-2.0

# Configure J-Link for nRF54L05
board_runner_args(jlink "--device=nRF54L05_M33" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)

