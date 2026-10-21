# Native read-only UFS transport

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
is checked for contiguity and inclusion in the loader-advertised kernel RAM
arena. Firmware-reserved RAM and physical guards remain unchanged. Cache lines
are cleaned before DMA and cleaned/invalidated after completion, with barriers.

## Read contract and failures

`b2l_ufs_transport_read(lba, blocks, out)` uses **native device blocks**, not BSD
512-byte sectors, and accepts at most `B2L_UFS_MAX_READ_BYTES` (64 KiB) per call.
The BSD adapter provides any smaller-sector view. All accesses are serialized,
checked against actual device geometry, and copied out of private DMA memory.

Only TEST UNIT READY, REQUEST SENSE, READ CAPACITY and READ commands are allowed.
There is no disk-write API. Samsung UTRD lengths and offsets use the working
firmware's byte units; generic UFS DWORD-offset descriptors are not substituted.
Slot and task tag 0 are used with only one outstanding command.

Every command has a five-second deadline and validates OCS, response transaction,
LUN, task tag, SCSI status, residual count and overflow/underflow flags. A pending
transaction or fatal controller error poisons the transport: its permanent DMA
memory stays pinned and cannot be reused for a new command. A failed command is
never treated as a successful disk read. A bounded initial REQUEST SENSE sequence
can clear a pending UNIT ATTENTION before reading capacity.

Cold initialization, power management, link recovery and disk writes are outside
this transport. If the inherited link is unavailable, initialization fails
explicitly. GPT validation and BSD device publication are separate from this
transport; neither a successful capacity read nor a GPT read proves an HFS mount.

## Host validation

Run `bash scripts/test-ufs-transport.sh` for ASAN/UBSAN tests of the actual
descriptor builder and polling state machine, including high DMA addresses,
byte offsets, invalid write opcodes, timeout/wraparound, fatal status, response
mismatches, and bounds. The same tests validate EFI vendor-path decoding and
reject truncated, malformed, unknown-controller and invalid-LUN paths. Apple DT
tests additionally check the exact four-byte LUN property and its omission for
filesystem fallback.
