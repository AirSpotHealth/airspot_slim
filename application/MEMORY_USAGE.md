# Production memory usage

The target is nRF54L15: 1,524 KiB NVM and 256 KiB RAM. The pinned SDK reserves
96 KiB NVM and 68 KiB RAM for auxiliary-core use, leaving 1,428 KiB NVM and
188 KiB RAM in the application domain.

MCUboot uses 64 KiB NVM. Two 664 KiB slots support signed test updates/rollback;
36 KiB holds settings. Image headers/trailers reduce the payload budget inside
each slot. See ../docs/OTA.md for exact addresses.

| Build | Application program bytes | Application RAM bytes |
|---|---:|---:|
| 0.14.1 baseline (e3d0c44) | 263,708 | 86,024 |
| 0.14.3 audit candidate (d69f516) | 262,892 | 85,976 |

MCUboot uses 32,124 program bytes and 19,872 RAM bytes. Its RAM is reused by the
application after handoff; do not add the two RAM figures together. These are
linker allocations, not stack high-water or heap-fragmentation measurements.

Run `bash scripts/airspot memory` from the repository root after a build for the
current artifact report. RTT remains enabled. Do not enable bootloader LTO:
the qualified GRTC handoff wrapper requires its current linking configuration.
