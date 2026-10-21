# Volatile development KeyStore

## Scope

Build 0029 reaches real launchd boot tasks, then keybagd requests recovery because
the system bag cannot be loaded and no AppleKeyStore service exists. Build 0030
adds an original, bounded IOResources service named AppleKeyStore and a type-0
AppleKeyStoreUserClient for the current no-passcode HFS development root.

The service is available only when ARM64e compatibility is enabled and global
code-signature enforcement is disabled. It represents an already-loaded,
volatile, unlocked system handle 0. It implements no SEP, encryption, persistent
keybag format, hardware authentication or wrapped-key operations. Unsupported
operations return an error rather than fake encrypted output.

## Evidence and ABI

Local analysis of the installed iOS 26 AppleKeyStore client and keybagd confirms
the type-0 connection and the empty selector-0 handshake. Selector 14 takes no
scalar or structure input and returns one scalar system handle. Selector 7 takes
one scalar bag handle and returns one scalar lock state. The development state
uses the unlocked-since-boot bit 0x4.

Successful get-system returns through keybagd's already-loaded route and skips
loading, creating or saving systembag.kb. On the current HFS root without data
protection, its subsequent bag-mapping helper also returns without cryptographic
operations. The no-effaceable-storage property belongs to the file-based bag
load/save route; it is not needed for this already-loaded route. Existing EFI
and root contents can therefore be reused.

A5 provides the comparison design. Its broad selector success behavior and
placeholder serialized bag are not copied. Current iOS argument shapes are
checked independently. Device acceptance still requires a new runtime screen.

The implemented external selectors are only 0, 7 and 14. Device-state DER
queries 17/35 and notification registration remain unsupported. Numeric
selector/result logs saturate globally after 24 calls and contain no payloads.

## Validation boundary

Run `bash tests/test-development-keystore.sh` for the host checks.

The portable dispatch core and the actual IOKit adapter are tested for argument
counts, output capacity, connection lifecycle and unsupported operations. A
complete class harness also checks development gates, connection type, provider
matching, allocation failure and close/free behavior. Native
compilation checks the current XNU IOUserClient signatures. Package readback
proves installed bytes; it does not prove keybagd or later service behavior.

The secondary native halt/restart implementation remains a separate follow-up.
The working Exynos UEFI source identifies its PMU reset sequence, but it has not
been executed by this XNU platform and is not added in this build.
