# Bench-only STCC4 measurement failure

Explicit Zephyr module for an isolated build. Link wraps the real STCC4 read
function; 60 seconds after its first measurement read every measurement read returns
-EIO. The startup window permits normal image confirmation before faulting.
Each reboot repeats that window. This does not emulate bus hangs, sensor sleep
failure or physical damage. Normal firmware does not include this module.

Build with application_EXTRA_ZEPHYR_MODULES pointing to this directory and a
separate output directory. Do not distribute this image. Restore the checked
normal build after testing. Preserve settings and record the device backup path.
