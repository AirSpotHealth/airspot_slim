# Production cleanup inventory

Baseline: e3d0c44, signed 0.14.1. Verified artifact copies are retained locally under `local_backups/audit-baseline-e3d0c44/`.

Removed files below are absent from the production CMake/sysbuild graph, CLI, editor tasks and artifact tests. Git retains their history. No old_firmware, SDK, signing material or user inputs were removed.

- `_working_minimal_uart/main.c`
- `_working_minimal_uart/nrf54l15dk_nrf54l05_cpuapp.overlay`
- `_working_minimal_uart/prj.conf`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/Kconfig.board`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/Kconfig.defconfig`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/airspot_slim_nrf54l05_cpuapp.dts`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/airspot_slim_nrf54l05_cpuapp_defconfig`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/board.cmake`
- `application/boards/arm/airspot_slim_nrf54l05_cpuapp/board.yml`
- `application/boards/nrf54l15dk_nrf54l05_cpuapp.overlay`
- `application/child_image/mcuboot/boards/nrf54l15dk_nrf54l05_cpuapp.overlay`
- `application/prj.conf.backup`
- `application/prj_debug.conf`
- `application/prj_release.conf`
- `application/prj_rtt.conf`
- `application/prj_uart.conf`
- `application/prj_verbose.conf`
- `application/src/main.c.backup`

Retained legacy protocol fields and factory commands for mobile compatibility. Mesh and power-manager paths remain until their callers are safely reconciled; disabled does not by itself mean safe to delete.
