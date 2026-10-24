# Build 0073: pinned extension-cache AMFI IOKit query

## Root evidence

0072 lsd reports that /System/Library/xpc/extensions.plist cannot load because
AppleMobileFileIntegrity is absent. The existing kernel AMFI metadata/MAC
policy does not publish that IOKit service. Sharing repeatedly calls the native
AppExtensionIdentity matching API for share-services, ui-services and services;
when LS point lookup fails, ExtensionFoundation produces code13 and Sharing
retries, including a fresh SFPerformDataMigrationIfNeeded call each time.
No migration is necessary, yet retries accumulate transaction/listing traffic.

Recovery read-only comparison confirms that both the98,192-byte plist and its
1,748-byte detached signature exactly match the original iPhone17,2/23G90 IPSW.
The HFS volume header is unchanged; no journal is replayed. The plist contains
126 definitions, including all three requested extension points.

## Native contract and bounded development policy

The native client matches AppleMobileFileIntegrity, opens type0, and calls
selector6 with scalar[1], a20-byte CDHash and no outputs. Success logs trusted;
NotFound is a nonmatch; other errors propagate. The missing-interface fix
therefore cannot safely be a service that returns success to every request.

B2LAMFI.cpp publishes that class only while the existing B2L unsigned-development
AMFI gate is active. Opening requires type0, platform classification and the
original com.apple.private.amfi.can-check-trust-cache boolean entitlement.
Every method rechecks the current task's platform/entitlement/development gate,
so a transferred connection does not skip admission checks. Calls after close
or before start fail. The entitlement object's retained reference is released.
Close/start publication uses acquire/release atomics.

Only two owner-selected local pins for the exact original extensions.plist
CodeDirectories are admitted: its primary SHA1 CDHash and alternate SHA256
CDHash truncated to20 bytes. Both CodeDirectories'24 page hashes are verified
against the exact plist/signature pair before inclusion. These pins are a local
B2L development admission policy, not Apple CMS verification or an Apple trust
chain. The original detached signature has no supplied Apple CMS trust proof.
Unknown CDHashes return NotFound. Other selectors remain unsupported; malformed,
async, output-producing, wrong-size, wrong-flag and short descriptor reads fail.
The service does not modify any trust cache, signing flag, entitlement, metadata
file, source plist or sandbox policy. Its registry property/log declares the
pinned development scope. Code/signature payloads are not embedded in source.

The loader, firmware, private WiFi metadata, CPU topology, native-cache workers,
KeyStore, sleep policy and debugger fix are reused from0072.

## Checks

The complete actual IOService/client compiles under an ASan/UBSan API mock.
Checks cover registration, property/base-init failures, provider/type/lifecycle,
all caller gates, balanced entitlement references, both pins,320 single-bit
hash mutations, unknown hashes/selectors, direct/descriptor inputs, short reads,
23 malformed ABI shapes, guarded outputs and revocation after opening.
The production XNU build caught the OSPtr raw-pointer ABI in this configuration;
the implementation and mock use its actual retained OSObject* contract.
Independent provenance checks verify exact plist/signature SHA256 digests,
primary/alternate CodeDirectories, content pages and compiled pins.
The48-patch sequence is validated; the new catalogue entry stays inside B2L.

## Installation and required runtime evidence

Only BOOT is updated after target recovery/product/root/partition/capacity
checks, a full BOOT backup and verification of the installed0072 prefix.
An exact image readback hash is required before reboot. Personalized outputs
remain ignored/private; no factory WiFi address is published or pushed.
The flash receipt is out/0073/flash-receipt.json.

Runtime must show B2L AMFI IO registration and an entitled lsd selector6 pin
match, disappearance of extension-cache load/code13 errors, and successful LS
extension-point records. Sharing retry rate and DataMigrator transaction count
must then be compared with0072. Service registration/build success alone does
not prove the loop or overall UI performance is fixed. Speech-plugin container
bootstrap, CoreImage limits and device-class issues remain separate.
