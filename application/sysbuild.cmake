# The bootloader must never inherit DK pin assignments on the product.
set(mcuboot_DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot.overlay"
    CACHE STRING "Shared AirSpot wiring" FORCE)

set(mcuboot_EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/boot_support" CACHE STRING "Product boot handoff" FORCE)
