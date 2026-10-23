# Build 0055: IPC copyout and reply-right diagnostics

This build changes opt-in diagnostics, not IPC results, credential policy or
container semantics. It does not suppress the native lsd container abort.

## Receive address correction

The old receive hook used the original receive-vector base after native
copyout. MACH64_RCV_STACK can place a successful message near the end of that
allocation. Patch 0043 moves capture into ipc_kmsg_put_to_user, after every
native message/trailer/aux copy succeeds, using the actual msg_rcv_addr.
Failed copyout and native body errors do not emit a successful capture.
Original buffer fields and native results remain untouched.

IPCRCV records retain actual/base addresses, capacity, options and the audit
trailer bytes returned by native IPC. Trailer copy failure is a diagnostic
field, not an IPC error. Existing bounded message/raw-data budgets remain.

## Reply-right lifetime

IPCPORT records snapshot permuted kernel port tokens before receive copyout
replaces pointers with names, and after successful user-send copyin. Initial
lsd send-once reply endpoints are remembered in a fixed 128-token table without
retaining references or dereferencing those tokens later. Address reuse can
alias tokens: correlate process, local name and event order.

SONCE records snapshot a destroyed reply right's receiver name and context.
Only scalar fields are read under the original port lock. After native unlock,
a bounded kernel stack is captured. User stack reads are deferred until the
current native Mach trap or BSD syscall returns, avoiding user faults under
IPC locks. An empty-pending fast path uses one atomic load. Pending events are
bounded and later events on the same thread may replace earlier ones before a
checkpoint; the records are not a complete lifetime trace.

The decoder retains delivery and right-lifetime records alongside payloads.
Private runtime captures remain local in ignored output directories.

## Verification

- Actual diagnostic C code passes AddressSanitizer/UndefinedBehaviorSanitizer,
  including adjusted receive locations, trailer auditing, tracked-right
  selection and deferred user backtraces.
- The actual prepared XNU ipc_kmsg_put_to_user body passes sanitizer tests for
  scalar, stack-vector, split payload/trailer/aux, copy failures, insufficient
  capacity and preservation of native errors.
- The 43-patch sequence passes incremental application, repeated preparation
  and refusal to overwrite unrelated edits.
- DEVELOPMENT ARM64 B2L kernel builds. BOOT packaging uses the existing touch
  firmware/loader and unchanged memory reservations and CPU topology.

Device installation receipts and subsequent runtime evidence are kept private.
Build/installation does not establish successful container lookup or GUI boot.
