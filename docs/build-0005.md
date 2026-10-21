# Build 0005: required Apple DT containers

## Device evidence

Build 0004 passes the boot entropy check and reaches arm_vm_init. The user
photograph identifies its next assertion at arm_vm_init.c:1321: the mandatory
lookup of chosen/memory-map did not succeed because the emitted DT omitted it.

## Fix and audit

The loader now emits a name-only /chosen/memory-map node beneath /chosen.
TrustCache and AuxKC properties are optional on the checked non-SPTM path and
remain absent because no corresponding data is loaded.

The same source audit found /defaults is mandatory for pmap ASID, PV and I/O
configuration lookup. A name-only /defaults node is now emitted at the root;
optional overrides remain absent so XNU keeps its existing defaults. The
/chosen DRAM fields and /cpus boot CPU properties were already present.
The normal-RAM arena and all reference physical guards remain unchanged.

## Verification

Host tests parse the actual emitted tree and verify both full paths exactly
once, the complete 256-byte seed and node/property boundaries at every buffer
capacity. No TrustCache, AuxKC or I/O mapping override is invented. These tests
pass under ASAN/UBSAN, and all 16 Python tests pass. The kernel source and kernel
binary are unchanged from build 0003; only the loader/ADT is rebuilt.

The next device test must confirm progress past VM initialization and then
actual interrupt delivery. Raw photographs, logs, identifiers, generated images
and Apple proprietary build inputs stay outside Git.

The firmware and loader rebuilt successfully. The validated image is ignored
`out/0005/boot.img`, 22,518,800 bytes, within the confirmed
57,671,680-byte BOOT capacity. The packager verified the exact loader in the
compressed firmware volume. Physical execution of this build is pending.

## Installation and device result

The BOOT-only write completed after preserving a full backup of the preceding
image. The original backup remains intact; the image prefix matched on readback
and the device was rebooted. The local receipt is ignored.

The user photograph confirms VM bootstrap completion, scheduler setup and later
kernel startup output through serial KDP and ulock initialization. A subsequent
data abort was symbolicated against this build's symbols to
crypto_random_kmem_ctx_size, called by vm_map_store_crypto_init. Its crypto
function table has not been registered. This is separate from the fixed DT
container assertions. The next change must provide the required allocator RNG
contract and PRNG startup without pretending unrelated crypto services exist.
Interrupt delivery has not yet been confirmed by the dedicated proof line.
