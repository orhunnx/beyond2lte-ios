# Build 0057: usable vnode capacity for a full root filesystem

The B2L xsmall configuration previously capped CONFIG_VNODES at 1024.
Native bsd_bufferinit computes desiredvnodes as sane_size / 65536 + 1024,
then clips it at CONFIG_VNODES. The low cap prevented normal RAM-based sizing.

Raise only the B2L cap to 65536. Native sizing, vnode recycling, allocation,
ENFILE behavior and VFS locking remain unchanged. This is a ceiling, not an
up-front allocation of 65536 vnode objects. Existing firmware/loader, CPU and
memory reservation settings are unchanged.

The kernel configuration is regenerated and the DEVELOPMENT ARM64 B2L kernel
rebuilt. The configured cap is checked in the generated compiler options.
Packaging and complete BOOT readback establish installation; the next device
capture must establish runtime capacity and whether service startup improves.
Private runtime captures remain local.
