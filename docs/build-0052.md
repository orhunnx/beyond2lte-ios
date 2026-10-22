# Build 0052: A5 parity paths, CPU scanout and physical input

## Changes applied

This build implements the list in `a5-parity-0051.md` against B2L's XNU 12377
and the locally inspected iOS 26.6.2 / 23G90 userspace. The A5 reference is
commit 506775feb4e06cfb0c2b1278324560fc2236dd94, not an ABI-compatible image.

| Area | Implementation | Important limit |
| --- | --- | --- |
| HFS regular-file swap | Patch 0037 retries ENOTSUP with VNOP_EXCHANGE for exact VFS_RENAME_SWAP, two regular files on the same HFS development root mount | Does not implement directory swaps or full inode/name exchange semantics |
| Image4 hash-method query | `/chosen/crypto-hash-method` is `sha2-384` | Supplies the property read by libimage4; no trust chain, manifest or cryptographic verification is invented |
| Entitlement queries | Patch 0038 supplies owned dictionary copies for process/vnode queries, and original-CDHash-gated F_GETSIGSINFO platform classification | Native signed metadata wins; no cs_blob, borrowed XML/raw-blob or internal pointer-return compatibility is created |
| IOSurface / framebuffer | Patch 0039 registers IOCoreSurfaceRoot, AppleCLCD/IOMobileFramebuffer and a CPU BGRA copy path | Single-plane only, no GPU, YUV or scaling; software 60 Hz notifications |
| HID and physical input | Patches 0039/0040 add event/queue/user-client scaffolding, GPIO buttons and sec_ts single-contact polling | On-device button, touch and GUI behavior still needs evidence |
| Container diagnostics | Patch 0041 logs up to 96 common csops completion records for container-manager callers; exec metadata logs are bounded | Not every early-return query is covered, and logging does not resolve all container failures |

Existing six-CPU topology, four native-cache workers, disk layout and all
reserved memory regions remain at their previous values. The persistent V2
cache is preserved. The narrow, approved 0051 CS_SIGNED query behavior remains.

## iOS 26 graphics contracts

The A5 implementation supplies the CPU-only service structure and public HID
source scaffolding. The shipped local iOS 26 framework code supplies these
updated wire layouts:

- IOSurface create/lookup reply: 0xc68 bytes, copied to client object +0x70.
  Base/state/timestamps are at reply +0/+8/+0x10; ID +0x18; allocation +0x20;
  64-bit width/height/stride +0x28/+0x30/+0x38; pixel offset +0x40;
  format +0x48; plane count +0x58; bytes-per-element +0x60.
- Limits selector 13: 0x28 bytes, 16-bit version 1 at +0x22. This differs from
  the A5 client's 0x18/+0x16 contract.
- IOMobileFramebuffer swap record: 0x588 bytes, swap ID +0x98, surface IDs
  +0x9c, source rectangles +0xac, destination rectangles +0x10c, mask +0x17c.
- Unsupported selectors and multiplane creation return Unsupported. Reply
  buffers are zeroed; real mapped allocation lengths bound pixel access.
- Framebuffer geometry/range comes from GOP/PE. The display nub uses the
  Samsung compatible string, not a fake Apple chip identity. Console drawing
  stops after the first accepted frame copy; panic drawing and logging remain.

IOSurface allocation is capped at 256 MiB per surface and 512 MiB aggregate
wired pixel memory. This is independent of the disk native-cache capacity.
The first accepted copy emits `B2L FB: first IOSurface frame copied to physical
scanout`. A successful copy still does not by itself prove a usable UI.

## Hardware input and firmware

The pin assignments come from the user's UEFI_beyond2lte keypad implementation:
GPA0.3 volume up, GPA0.4 volume down, GPA2.4 power and GPA0.6 Bixby. GPIO DAT is
read at base 0x15850000, active low, without mux writes. Sampling is every 20 ms
with two-sample debounce and an initial-state baseline. Bixby maps to Home.

The disposable firmware profile enables its existing B2LTouchDxe with
`B2L_ENABLE_TOUCH=1`; the reference UEFI repository is not modified. The native
input service reuses that driver's HSI2C controller/timing at 0x104b0000,
slave 0x48 and sec_ts parser. It requires initialized timing and boot-status
0x20, bounds each message to 5 ms, polls at most four events, and disables touch
after three failed polls while leaving buttons active. No touch firmware or
voltage programming is added in XNU. The existing firmware driver initializes
its documented rails and quiesces at ExitBootServices.

HID source provenance and retained notices are in
`xnu-overlay/iokit/Kernel/arm/hid/README.b2l.md`.

## Verification performed

- Native kernel and touch-enabled firmware build successfully; loader is
  verified inside the compressed firmware volume.
- Actual 41-patch preparation passes upgrade from 36, repeat/idempotence and
  refusal of a local native-source modification.
- Loader tests pass the 19-node DT, new properties, GOP bounds, arena ownership,
  GIC encoding and existing boot ABI checks.
- Development AMFI tests pass original metadata/CDHash parsing, malformed-input
  mutations, typed dictionary copies and process/credential ownership/lifetime.
- `tests/test_graphics_boundaries.py` executes the actual blitter, reply writer,
  limits selector body and HFS fallback body under ASan/UBSan. It covers clipped
  rectangles, negative/huge inputs, short allocations, 100,000 malformed
  rectangles, exact reply/limits extents, zeroed reply padding and fallback gates
  including native/exchange errors.
- These checks do not prove hardware service matching, touch transport, GUI
  presentation or disappearance of the previous userspace crashes.

## Package and installation

- BOOT bytes: 23302672.
- BOOT SHA256: `9e458d47b8e6ede6fa9d49ad8c24a3ae000e8787622186601181ea6343aaf34d`.
- Kernel SHA256: `8a202768c78ec4e59c2f04c683a5b5c5c74b784559c1b7f45557e79cdc865ee2`.
- Firmware SHA256: `3702059503e049d5c23bbae9d205e57c658c085c44249183916f62833bd8a6a0`.
- Loader SHA256: `0c2fe3420eb9032be179f0beecce85eceff84887ce0f08ea5ae6028ff46a75e3`.
- Kernel/unstripped/dSYM UUID: `4C468959-CF2B-3EAB-83AF-054FDFDEC671`.

Confirmed authorized recovery, beyond2lte identity, root, BOOT sda14 and its
57,671,680-byte capacity. Verified the complete current 0051 BOOT prefix and
retained its local rollback image. Wrote only BOOT and verified the complete
0052 prefix hash. USERDATA's 4096-byte header was unchanged; SYSTEM and USERDATA
were not written or mounted from recovery. Reboot was requested successfully.
The receipt is retained locally in `out/0052/flash-receipt.json`.

Runtime acceptance is pending. On the next capture inspect `B2L INPUT`,
`B2L FB`, `B2L SURF`, `B2L MCMQUERY` and `B2L EXECMETA`, then compare the original
lsd, AppProtection, ManagedSettingsAgent, mobileactivationd, passd and cryptexd
exit reasons. Do not equate a successful build or BOOT write with solved crashes.
