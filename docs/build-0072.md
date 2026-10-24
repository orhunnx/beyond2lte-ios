# Build 0072: NULL-safe early debugger logging

## Confirmed fault

0071 photograph reports PC `0xfffffff00730d2bc`, LR `0xfffffff00730d294`,
ESR `0x96000005`, FAR zero. The exact installed unstripped image maps PC to
`ldr x3,[x23]` in DebuggerTrapWithState, where x23 is db_panic_args.
DebuggerWithContext legitimately passes NULL format and argument pointers for
DBOP_DEBUGGER. The upstream early formatter unconditionally dereferences them
while PE_arm_debug_and_trace_initialized is false; B2L does not implement the
Apple debug/trace platform infrastructure even after userspace starts.
This explains this NULL access; it does not identify the debugger request's
original caller or prove all debugger handling is supported.

## Change

Append-only board patch0047 formats panic varargs only when both pointers are
present. Normal debugger messages use bounded snprintf with a literal format;
NULL messages have a safe fallback. Native DebuggerSaveState, TRAP_DEBUGGER,
interrupt assertions and debugger policy remain in force. B2L entry diagnostics
record reason, caller, context presence, options and at most160 message bytes
before platform panic hooks, without regular printf locks or allocation.

## Evidence and checks

The recovery export preserves0071 kernel epoch39:2,606,746 text bytes,
18 missing positions, no producer drops. The panic tail is not committed.
The read-only HFS export is complete with an unchanged source volume header;
all prior Persist hashes match0070 and the only added Persist file is empty.
There is no new native event evidence identifying the debugger trigger.
The kernel log contains the system-sleep-disabled marker and successful
KeyStore selector17 replies; native passcode UI acceptance remains unverified.

Seven ASan/UBSan cases run the actual patched formatter extracted from prepared
XNU: normal NULL varargs, literal format tokens in messages, real panic varargs,
each missing pointer independently, NULL message, truncation and initialized
infrastructure. Real XNU build passes. Final ARM64 disassembly checks both NULL
pointers before loading the va_list. The existing0071 firmware/loader is reused,
including private WiFi metadata, sleep policy and KeyStore correction.

## Installation

Only BOOT is updated, after checking exact target and the installed0071 prefix,
keeping a full BOOT backup and verifying the written0072 image hash. Artifact
and receipt remain in ignored out/0072; no real WiFi address is published.
Runtime stability and the original debugger request require the next device
observation; no claim of stable SpringBoard operation is made.
