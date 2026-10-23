# Build 0061: correct native CPU presentation masks

## A5 comparison

The A5 CPU path publishes an IOSurface root and AppleCLCD, receives native
IOMobileFramebuffer swap records, and copies submitted BGRA rectangles to the
physical scanout using CPU loads/stores. Its later presentation timer limits
copy cadence independently of 60 Hz software notifications. That timer is a
performance choice, not a missing renderer. Its iOS 16 record is 0x46c bytes;
its offsets cannot be copied into this iOS 26 client.

The shipped iOS 26 QuartzCore also contains a concrete software renderer.
AccelServer::render_update falls back to Server::render_update when its
accelerator renderer is absent. Server::sw_renderer constructs SWContext,
which imports IOSurfaces and implements software draw operations. This change
uses that existing path and does not advertise a fabricated GPU.

## Correct native swap masks

The iOS 26 kern_SwapEnd sends client + 0x18 with length 0x588.
kern_SwapSetLayer stores surface IDs at client + 0xb4, source rectangles at
+0xc4, destination rectangles at +0x124, update mask at +0x164 and disable mask
at +0x168. Thus the wire offsets are +0x9c, +0xac, +0x10c, **+0x14c** and
**+0x150**, respectively. The earlier +0x17c mask field actually corresponds
to client +0x194, a different field explicitly cleared by SwapBegin.

The old field could skip every layer even when a valid CPU frame was submitted.
The copy loop now requires an updated, non-disabled layer with a nonzero
surface ID. A bounded diagnostic records the actual masks and all four IDs,
including empty swaps. The existing mapped-allocation checks and first-copy
marker remain. No inference of a visible UI follows from a successful call.

## Verification

The host regression runs the actual swap and blit functions with client-object
fixtures using the shipped client-relative offsets. It checks all four layers,
disable with stale ID, the former wrong mask, padded source/destination rows,
short backing, invalid source coordinates, unsupported formats, absent IDs,
and balanced surface retains. ASan/UBSan and the existing framebuffer query
regression pass. Device installation and the first visible frame are separate
checkpoints; private logs and receipts remain local.

The parallel IOSurface property/attachment implementation is excluded from this
build. Build 0061 contains only the proven framebuffer-mask correction and
bounded swap diagnostics; it retains the complete 0060 surface implementation.
