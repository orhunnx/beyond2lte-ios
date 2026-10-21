# Build 0011: describe the existing non-CTRR boot policy

## Confirmed failure

Build 0010 passes TCP/AES initialization and Skywalk self-tests before BSD
initialization reaches kminit. The photographed assertion names
_unsafe_kernel_text_initialized in machine_routines.c:3379.

The DEVELOPMENT kernel initializes this state at TUNABLES/FIRST only when
/chosen/kernel-ctrr-to-be-enabled is present with the native int width.
The generated Apple device tree omitted that required field. B2L does not
implement Apple's hardware CTRR facility.

## Scope

This change supplies the actual existing policy in loader metadata. It does
not remove the kernel assertion or change the firmware physical memory/MMIO
guards. The kernel image from 0010 can be reused; EFI and its embedded firmware
must be rebuilt. SYSTEM remains untouched.

## Implementation and verification

The loader now emits exactly one native little-endian 32-bit zero value under
/chosen/kernel-ctrr-to-be-enabled. XNU consumes this through its existing
TUNABLES initializer. It records that no Apple CTRR is forthcoming; it does not
change ordinary MMU read-only or execute-never permissions.

All loader host tests passed, including ASan/UBSan device-tree checks for exact
property parent, width, value, occurrence count and insufficient capacities.
The standalone EFI and 4 MiB embedded firmware rebuilt successfully. Packaging
verified the exact new EFI bytes in the actual compressed firmware volume.
The kernel hash remains identical to 0010. The validated BOOT image is
22,559,168 bytes, within the confirmed partition capacity. The firmware memory
map is byte-identical to the original guarded reference.

Independent source review confirmed that B2L's active non-SPTM/non-PPL path
needs this field and no related hardware CTRR range. Optional Apple fuse or
production-status values were not fabricated. Kernel assertions remain intact.

## Installation and device boundary

The authorized BOOT-only update completed after a full verified backup of the
previous BOOT. The original backup remains intact. Exact image-prefix readback
matched, then the device was rebooted. SYSTEM remains untouched. Raw receipts
and identifiers stay local and ignored.

Physical progress past the policy assertion is pending.
The next screen report should show later BSD autoconfiguration or a root-device
wait; no root filesystem or native UFS driver is supplied by this update.

## Device result

The user's photograph confirms progress past kminit without the prior assertion.
Boot CPU registration and timer counts 42, 44 and 46 with SGI1=1 confirm that
interrupt delivery remains functional. DLIL and Skywalk self-tests complete;
loopback worker scheduling also proceeds. XNU then waits for IOMedia with
Content Apple_HFS. This is the expected root-device wait of a kernel with no
native storage/root filesystem provider, not a kernel panic.

The initial target of physical XNU boot, readable kernel output, CPU registration
and timed interrupt-driven scheduling has been achieved. A mounted root and
userspace are separate remaining milestones.
