# Conditioning across restarts (0.22.5)

The STCC4 22-second conditioning command and the first-hour white settling
indicator are separate. Completion of the initial hour stays completed across
updates; it is not evidence that the sensor can skip conditioning after a long
period without measurements.

The last successfully measured time is saved at most once per minute while a
valid phone clock is available, and at clean sensor shutdown. Startup no longer
erases that record. After a restart, phone time is used to reconstruct the boot
time and compare it with the previous sample, even if time sync arrives late.
An elapsed gap of exactly three hours skips conditioning; more than three hours
requests it. Invalid/backward phone time leaves the gap unresolved. Measurements
can continue while awaiting time, as before. Missing/corrupt history uses the
existing conservative conditioning fallback. No persistent real-time clock is
assumed across power loss.

Abrupt reset, debugger flashing, or power loss can leave the saved sample up to
one minute behind the actual last sample under healthy storage/sampling. Thus a
gap close to three hours may conservatively trigger conditioning up to a minute
early. Failed writes can extend that uncertainty; they retain the older record
and retry on the next minute, rather than falsely claiming a recent measurement.
Without a phone time sync, elapsed off-time cannot be determined.

Initial-hour progress is independently checkpointed after each minute of valid
sensing, with CRC validation. Restart resumes the saved progress and never adds
time spent powered off. An abrupt restart can lose less than a minute of progress
with successful writes. Completion records from previous firmware are accepted.
Devices predating the initial-hour record cannot retroactively prove completion.
Sensor factory reset still clears progress. Only confirmed images write progress.

Validation covers reboot checkpoints, delayed clock sync, exactly/over three
hours, missing/corrupt records, failed storage, retained legacy completion,
partial-hour resume, and exclusion of powered-off time. Physical elapsed-time
and OTA verification on a device remain separate from these automated tests.
