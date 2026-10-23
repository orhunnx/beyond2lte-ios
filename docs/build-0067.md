# Build 0067: continuing progress records behind the Apple logo

The SYSTEM console writer already continues while the Apple logo is visible.
Detailed IPC, backtrace and syscall records have finite startup quotas, however,
so lack of new detailed records is not proof that userspace stopped running.

0067 adds a fixed resident last-observation cache for SpringBoard, backboardd,
container managers, lsd, assetsd and tccd. Each role has16 thread slots. Current
threads update their own numeric syscall-entry, Mach-receive-entry, syscall-
completion, IPC-send/receive and IOKit-service-wait observations before the
existing detailed diagnostic quotas. No raw payload quota is removed.

The existing console worker emits the cached observations approximately every
30 seconds under b2l_diag=1, along with observed boot second, observation age,
PID/TID, boundary kind, arguments, saved current-thread PC and native error.
An accompanying summary reports published slots, busy snapshots and slot
capacity misses. The normal LIVE heartbeat now also appends boot seconds.
Events:1=syscall entry,2=Mach receive entry,3=syscall completion,4=IPC send,
5=IPC receive,6=IOKit service wait.

These are last observations, not remote live thread samples. A stale row does
not prove a thread is still alive or blocked inside its last recorded call.
No remote user-memory/stack reads, process lookup, task suspension, or allocation
is performed by the worker. All shared fields are atomic; version-checked
reader snapshots have at most three retries. Logging runs outside the storage
writer lock and never waits on a publisher. The existing16MiB on-disk ring still
has bounded retention; this change does not promise unlimited history.

The decoder retains the new records in progress_records. Actual host tests
pass ASan/UBSan, including sampling after20 minutes with no new hook, updates
after startup quotas are exhausted, disabled diagnostics, a busy publisher,
explicit capacity exhaustion, and12 concurrent publishers with a reader that
checks version consistency. Existing targeted diagnostic tests also pass.
The real DEVELOPMENT ARM64 B2L kernel builds and the progress markers are
verified in the actual Mach-O. Source and symbols remain reproducible locally.

0067 BOOT is 23373512 bytes, SHA-256:
55e6c9833abe280714223c02246ccbca241aea817fc54b839d2b5efc94d6a553.

The verified0066 firmware/loader prefix is reused byte for byte. This image
has not been installed: the user's running0066 half-hour observation is being
preserved. Read that runtime's logs before any later installation.
