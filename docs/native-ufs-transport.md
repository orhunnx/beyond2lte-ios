# Native UFS transport

The B2L transport uses the live Exynos 9820 UFS link left by the working EFI
driver. It runs independently of EFI after `ExitBootServices`. Successful host
tests and compilation do not establish successful device DMA or disk access.

## Entry contract

`b2l_ufs_transport_init()` returns the actual device block count, block size and
BOOT-derived LUN. The LUN must come from `/chosen/b2l,boot-ufs-lun`, decoded by
the loader from the selected BOOT partition's Exynos vendor device path. There
is no default LUN and no EFI callback or old descriptor pointer is reused.

The transport maps exactly the HCI page at `0x13d60000` and vendor page at
`0x13d61000`. It requires a running, ready controller, empty transfer and task
doorbells, 64-bit DMA capability, and the inherited Samsung byte-granular
descriptor configuration. It never resets HCE, PHY, link, regulators or clocks.
Controller interrupt enables are cleared for synchronous polling.

Fresh contiguous kernel memory contains a transfer-list page, stopped task-list
page, command/response/PRDT page, and a 64 KiB bounce buffer. Every physical page
is checked for contiguity and inclusion in one of the EFI-allocated owned RAM
spans. The primary bootstrap arena is not the only valid DMA bank. Physical
holes, reservations and buffers that cross a span boundary are rejected before
controller takeover. Cache lines
are cleaned before DMA and cleaned/invalidated after completion, with barriers.

## Read contract and failures

`b2l_ufs_transport_read(lba, blocks, out)` uses **native device blocks**, not BSD
512-byte sectors, and accepts at most `B2L_UFS_MAX_READ_BYTES` (64 KiB) per call.
The BSD adapter provides any smaller-sector view. All accesses are serialized,
checked against actual device geometry, and copied out of private DMA memory.

Build 0012 allowed only TEST UNIT READY, REQUEST SENSE, READ CAPACITY and READ.
The 0013 extension additionally permits bounded WRITE(10)/(16) and actual
SYNCHRONIZE CACHE(10). Write requests are admitted only through the BSD layer's
validated HFS SYSTEM capability. Whole-LU and other partition views remain
read-only. Samsung UTRD lengths and offsets use the working
firmware's byte units; generic UFS DWORD-offset descriptors are not substituted.
Slot and task tag 0 are used with only one outstanding command.

Every command has a five-second deadline and validates OCS, response transaction,
LUN, task tag, SCSI status, residual count and overflow/underflow flags. A pending
transaction or fatal controller error poisons the transport: its permanent DMA
memory stays pinned and cannot be reused for a new command. A failed command is
never treated as a successful disk read. A bounded initial REQUEST SENSE sequence
can clear a pending UNIT ATTENTION before reading capacity.

Cold initialization, power management and link recovery are outside this
transport. If the inherited link is unavailable, initialization fails
explicitly. GPT validation and BSD device publication are separate from this
transport; neither a successful capacity read nor a GPT read proves an HFS mount.

## Host validation

Run `python3 tests/test-ufs-owned-ram.py` for native initializer tests with
mocked allocation/translation: low DDR, extra upper DDR and the primary bank
are admitted; reservations, crossing extents, overflow and physically fragmented
mappings are rejected before takeover.

Run `bash scripts/test-ufs-transport.sh` for ASAN/UBSAN tests of the actual
descriptor builder and polling state machine, including high DMA addresses,
byte offsets, data-in/data-out direction, write CDB bounds, real flush commands,
timeout/wraparound, fatal status, response
mismatches, and bounds. The same tests validate EFI vendor-path decoding and
reject truncated, malformed, unknown-controller and invalid-LUN paths. Apple DT
tests additionally check the exact four-byte LUN property and its omission for
filesystem fallback.

## Write completion and durability (0013)

Data-out requests copy CPU input into the kernel-owned bounce buffer before
cache cleaning and the doorbell write. WRITE(10)/(16) uses the actual Samsung
byte descriptor layout, UPIU write flag and UTRD data-out direction. Strict
completion/error checks and poisoned outstanding-DMA handling are unchanged.
No discard, query modification, firmware update or format opcode is admitted.

FUA capability has not been measured. Every successful native write therefore
issues non-IMMED SYNCHRONIZE CACHE(10) before reporting success. The separate
sync operation issues the same real command; unsupported/failed flushing returns
an error. The first completed write plus cache flush produces a single proof
line without data or addresses. A passing descriptor test or an enabled write
permission is not physical device-write proof.
