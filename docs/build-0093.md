# Build0093: report actual IOSurface limits to CoreImage

## Proven failure path

0092's native archive contains8 SpringBoard CoreImage failures for14,400 pixels,
paired with `destination 12x12 too big`. The owner reports malformed icons,
working touch, and a completely black screen after tapping Settings; a short
power-button press does not restore it. The exact Settings-launch/failure event
is absent from the retained log, so the black-screen cause remains unproved.

Read-only analysis uses the original iPhone17,2/23G90 SystemOS shared cache.
Extracted analysis binaries and disassembly remain private in ignored
out/research-0093; they are not distributed as source or patches.

The shipped IOSurface client receives a40-byte selector13 reply at
0x1b22942a0..0x1b22942ec. It checks ABI version1 at+0x22.
IOSurfaceClientGetPropertyMaximum (0x1b2295038) then reads64-bit maxima:

| Reply offset | Native property | Previous B2L result | Corrected result |
| --- | --- | ---: | ---: |
| +0x08 | BytesPerRow / PlaneBytesPerRow | 0 | 268435456 |
| +0x10 | Width / PlaneWidth | 0 | 16384 |
| +0x18 | Height / PlaneHeight | 0 | 16384 |

The existing alignment masks at+0/+4 and ABI version at+0x22 are retained.
CoreImage iosurface_limits at0x19e107bac caches property maxima.
CI::SWContext::max_texture_size (0x19e0de96c), max_input_image_size
(0x19e0de994) and max_output_image_size (0x19e0de9bc) consume the cached
width. The native render path compares output dimensions to that limit before
its `destination %dx%d too big` failure at0x19e1a8c80. Thus the previous
zeroed response rejects every nonempty CPU image. This is an ABI defect in
B2L, not a missing CPU renderer or evidence of an oversized12x12 allocation.

## Change and scope

Selector13 now reports the same dimension ceiling already enforced by
b2l_surface_layout. The maximum possible single row is bounded by the existing
256MiB per-allocation ceiling. That value is a property bound, not allocated
memory or a raised memory budget. The256MiB allocation ceiling and512MiB
aggregate wiring budget remain. Format, plane, geometry and mapped-buffer
checks are retained. Unknown GPU/notification methods are not turned into
success responses and no iOS binary is modified.

The query rejects positive input/output scalar counts and structured input.
It preserves native MIG's non-null zero-count backing behavior, uses the existing
checked inline/descriptor output writer, and logs the successful limits once.

Framebuffer presentation also retains cumulative best-effort atomic counters:
swap records, successful layer copies, missing surfaces, unsupported layout or
mapping, bounds failures, disabled-layer records and last swap/masks/format.
One summary per600 software-vsync callbacks distinguishes continued input from
failed/stopped presentation beyond the existing first32-record logging cap.
It samples no pixels, adds no presentation lock, and does not change pixels,
vsync scheduling, layer handling or fallback rendering. These are diagnostic
counts, not a coherent frame transaction or hardware-vsync proof.

## Verification

ASan/UBSan executes the actual selector13 case, checked output writer and
production constants. An independent client-layout fixture reproduces the old
zero-limit rejection of12x12, then accepts12x12,1440x3040 and the16384 boundary
with the corrected response;16385 rejects. Inline/descriptor canaries, short
buffers, short descriptor writes and malformed query shapes pass. Existing
blitter/reply/HFS boundary tests, native framebuffer swap tests and query ABI
tests pass. Presentation tests exercise missing, unsupported, disabled and
out-of-bounds counters with balanced surface ownership and unchanged scanout.

The production kernel compiles and links. Scalar-cache codegen/direct-VM-API
audit passes and the linked kernel has zero RCpc instructions. The verified
0092 firmware/loader is reused;0092's fast direct VM checks, SipHash and bounded
pairing are retained. Actual disappearance of CoreImage errors and successful
Settings presentation still require fresh device evidence.

## Installed

0092's kernel region and complete native/supplemental snapshot were saved
before writing. Recovery/product/root/BOOT path and57,671,680-byte capacity
were checked; a full BOOT backup and exact0092 image-prefix comparison passed.
Only BOOT was written. The23,535,936-byte image and exact readback
share SHA256:

`11d78ae62466b579e6d75bd4d1ad999c802fe67efc7f3d66748b4f0397b0e239`

The reboot command completed. Private image, backup, symbols and installation
receipt remain under ignored out/0093. Runtime resolution of malformed icons
and the Settings black screen is pending; installation does not prove that
the complete SpringBoard display path is fixed.
