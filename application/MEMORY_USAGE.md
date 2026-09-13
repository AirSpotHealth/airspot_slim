# Production memory usage

The target is nRF54L15: 1,524 KiB NVM and 256 KiB RAM. The pinned SDK reserves
96 KiB NVM and 68 KiB RAM for auxiliary-core use, leaving 1,428 KiB NVM and
188 KiB RAM in the application domain.

MCUboot uses a 64 KiB NVM partition. Two 664 KiB slots support signed test
updates/rollback; 36 KiB holds settings. Image metadata reduces the linker's
application region to 659,306 bytes. See [OTA layout](../docs/OTA.md).

## Linker-region allocation

| Build | Application flash bytes | Application static RAM bytes |
|---|---:|---:|
| Hardware-tested 0.17.1, 6 September 2026 | 270,908 | 89,496 |
| Experiment cleanup, 13 September 2026 | 269,896 | 89,376 |
| Reduction | 1,012 | 120 |

The cleanup build uses 40.94% of its flash region and 46.43% of its 192,512-byte
RAM region. Remaining region capacity is 389,410 bytes flash and 103,136 bytes
RAM. This is static allocation headroom, not measured runtime stack/heap margin.

MCUboot's linker allocation is 32,124 bytes flash and 19,872 bytes RAM. Its RAM
is reused by the application after handoff; do not add the two RAM figures.
The cleanup was built and checked locally, not flashed or hardware-qualified.

`bash scripts/airspot memory` verifies the current artifact and reports ELF
sections. For this cleanup, ELF text+data is 269,876 bytes and data+bss is
89,443 bytes; ELF section totals differ slightly from linker-region accounting.
Use consistent accounting when comparing builds.

RTT remains enabled. The experimental qualification/resource-metrics profile
has been retired. Do not enable bootloader LTO: the qualified GRTC handoff wrapper
requires its current linking configuration.
