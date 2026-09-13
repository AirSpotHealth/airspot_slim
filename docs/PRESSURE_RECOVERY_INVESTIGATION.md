# Pressure recovery investigation

Target: AirSpot-C58CC9, confirmed 0.17.1 image
44d7eff174e3aac57df502055524c57ee7d41b95a903b2d5853aaaa1aa9c0251.

Observed: after supply interruption during a bootloader sector move, LPS22HH
WHO_AM_I transaction at 0x5D failed with -EIO on both the trial and rollback image. SHT40
and STCC4 still responded; BLE worked. A subsequent reported 30-second cold
off restored WHO_AM_I=0xB3 and pressure readings. This narrows the failure to
the pressure sensor/interface/power path, but does not prove a cause.

Current driver returns immediately on failed WHO_AM_I. Its existing soft-reset
recovery addresses first-sample timeouts only, when register access works. A
register reset cannot be assumed to recover an address that does not ACK.

Rev3A schematic sheet 7 shows shared 3V0 VDD/VDDIO, 100 nF + 4.7 uF local
decoupling, 330 kohm CS and SA0 pull-ups, and INT_DRDY to NRF_LPS_INT. Sheet 5
maps NRF_LPS_INT to P2.10. Read-only live P2.10 PIN_CNF was 0x2 (input buffer
disconnected, no pull), so GPIO IN cannot establish its physical pin voltage.
Do not change this pin to an output: it is a sensor interrupt output.

ST LPS22HH datasheet Rev2 section 7.5 says INT_DRDY high during startup selects
I3C-only; for I2C it must be unconnected or pulled down during initialization.
ST recommends setting IF_CTRL.I3C_DISABLE after initialization. Current driver
leaves IF_CTRL=0 (also observed in successful boot logs). This is a configuration
hardening candidate, not proof that I3C selection caused the observed failure;
it cannot repair a sensor already inaccessible before the first write.
Source: https://www.st.com/resource/en/datasheet/lps22hh.pdf

Next experiment: a brief supply-output interruption on the same confirmed
firmware, with no OTA and no flash writes requested, to separate power-startup
behavior from swap recovery. Keep a failed sensor powered if reproduced, so
its address/pin/controller state can be inspected before another cold reset.
No production firmware changes have been made during this investigation.

The SDK TWIM driver maps both transfer timeout and negative completion results
to -EIO. Existing logs therefore do not establish address NACK versus data NACK
or a timeout. Do not label this an address-NACK diagnosis without capturing the
controller result or a bus trace. A timeout already invokes the SDK bus recovery
routine; adding another blind bus-recovery loop is not an established fix.

Added a host regression case for the observed unreadable WHO_AM_I condition:
initialization fails without marking health or resetting the sensor, and a later
explicit init succeeds once reads recover. The actual-driver test passes. This
validates failure handling, not automatic hardware recovery or a root-cause fix.
