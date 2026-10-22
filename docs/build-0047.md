# Build0047: non-NULL parameters for the bidirectional xcall proof

## Device evidence and correction

0046 reached checkpoint18 on CPUs1..5. The first panic was on MPIDR0x100:
`cpu_xcall_internal: cannot have null func/param`, with a non-NULL function and
NULL parameter. The proof encoded its target CPU directly as a pointer; the
secondary-to-CPU0 check therefore passed NULL. XNU uses a non-NULL parameter
as the xcall publication sentinel and rejects that call before dispatch.

Encode target+1, validate the encoded range, then decode target in the callback.
The kernel assertion and xcall implementation remain unchanged. This is a
correction to our verification code. CPU affinity, PSCI entry, timer/SGI proof
requirements, six-core topology and single cache worker are unchanged.

## Build and installation

Incremental kernel build succeeded. The modified object and kernel timestamps
are current; stripped, unstripped and dSYM UUIDs match: 331F70D3-830C-3DAB-92CA-09B033D2D665.
Only BOOT was written after the complete0046 prefix matched its package hash.
Complete0047 prefix readback matched `b8d27cf4d621996ec4982010505acd7521f083705e499b3c6306260350821d50`; reboot requested.
The existing firmware/loader were reused and0039 rollback remains available.
No broad optional tests were repeated, as requested.

## Device result

The user supplied a0047 screen showing CPU0..CPU5 timer and remote xcall
verification and the final `6/6 CPUs initialized, per-CPU timer and bidirectional
SGI1 verified` line. This confirms the six-core startup checkpoint: four A55
and two A75; Samsung M4 cores remain excluded. It does not establish full iOS
UI operation or long-run SMP stability. The original screen is retained only
in ignored local output. Cache preparation parallelism may now proceed.
