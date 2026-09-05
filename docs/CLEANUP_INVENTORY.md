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

Retained legacy protocol fields and factory commands for mobile compatibility. Mesh paths remain pending a compatibility decision. The unused power-manager tap/hold/System-OFF implementation was subsequently replaced by an atomic soft-off request consumed by main; only main initialization and the protocol sleep command referenced it. Existing button gestures remain in main.

Removed two additional unused setup helpers after reference inspection:
- `application/setup_env.sh`: superseded by scripts/airspot; auto-installed global Python dependencies. Updated historical documentation references.
- `application/scripts/ensure_mcuboot_key_link.sh`: superseded by the absolute signing-key path passed by scripts/airspot; no production caller. Actual signing keys and existing SDK paths are untouched.

Replaced obsolete nRF54L05 memory guidance with generated nRF54L15 baseline/candidate figures.
