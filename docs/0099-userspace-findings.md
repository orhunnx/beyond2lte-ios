# 0099 runtime: Settings opens; profile, rendering and install gaps remain

The owner confirms Settings opens after 0099 and supplies a photo. Icons are
blank gray tiles and the Misconfigured Device alert text appears without its
expected backing material. These are observed rendering failures, not a fully
working Settings checkpoint. The photographed alert names
/System/Library/EventTimingProfiles/.Default.plist.

## Capture

The read-only native/supplemental export is complete with errors=[] and an
unchanged HFS header. Native boot 9A8CB574-6757-4F0E-B474-68AE40B21ECB contains
241,579 decoded events. Kernel epoch60 contains 1,433 valid records, 23 missing
positions and 29 producer drops. The captures retain their limitations: late
unflushed records can be missing. Private logs and device identifiers stay in
ignored out/ directories.

## Event timing resource selection: confirmed cause

The original source image includes D94.Default.plist (42-byte binary empty
dictionary) and D94.Touch.plist (347 bytes), not .Default.plist. UIKit's
_makeProfilePath at 0x189211ab0 reads hw.targettype at 0x189211af0..1b0c and
constructs %s/%s.%s.plist at 0x189211b14..1b34. The native hw.targettype sysctl in
IOPMrootDomain.cpp reads the service root's target-type OSData property and
returns an empty string when it is absent. Our loader omitted that property.
Thus the empty target component causes the exact path photographed by the
owner. The original iPhone17,2 device tree supplies target-type D94.

0100 adds that missing resource-selection value to the loader. The shipped
D94 profiles can then be selected directly. The UIKit alert's suggestion to
create an empty file was not taken as an instruction from the owner.

## Application helper: now executes, but native staging rejects HFS

The arm64e v2 helper enters main, enumerates 50 staged candidates and invokes
SystemAppMigrator. Its native result is false; installedAfter remains zero.
The v1 attempt receipt records attempted=true, complete=false and is preserved.
The helper does not retry a recorded failed migration every boot.

The native chain reports MIInstallerErrorDomain code166:
Expected install path file:///private/var/ to be located on an APFS formatted
volume; got hfs. This appears through installcoordinationd and the original
MobileInstallationHelperService MIStagingManager path. Disassembly of the
shipped helper at 0x1000032c0..32d8 compares the actual filesystem type with
apfs; the failure path at 0x1000032dc..3330 creates the observed error.
No legacy/non-APFS option guards that comparison in the inspected method.

The migration also reports desired state6 and skips already-uninstalled apps;
some existing data containers trigger demoted-placeholder attempts that fail
at the same filesystem gate. Fixing the staging requirement alone therefore
cannot yet be claimed sufficient for installing all 50 candidates. Native
eligibility/default state and a real HFS staging/copy integration must be
resolved before another installation trial. The current evidence establishes
failure, not success or an entitlement-only problem.

## Rendering remains unresolved

The native boot contains 42 verify_image_parameters invalid image colorspace:
NULL messages and 328 invalid-context messages. Native CoreGraphics backtraces
are present, but most internal names are redacted; no complete address chain
identifies the underlying producer of the NULL space. These warnings are
captured in SpringBoard. They are not proof of the exact cause of Settings'
missing icons or alert backing. No earlier CoreImage zero-size-limit error is
observed. MobileGestalt's regenerated iPhone artwork profile remains present.

Framebuffer counters show accepted BGRA copies with no unsupported-format or
bounds failures in the sampled progress lines. IOSurface selector17 requested
by iconservicesagent remains unsupported; original-client disassembly identifies
it as surface notification registration, not a pixel draw operation. Do not
return fabricated success or label it the icon cause without the caller's
error path. The source contains a native CPU renderer, but feature coverage
for this scene still needs investigation.
