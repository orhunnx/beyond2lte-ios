# Build 0070: private WiFi address metadata

## Native contract and scope

The shipped libMobileGestalt WiFi-address lookup first reads CFData from
`IODeviceTree:/chosen`, property `mac-address-wifi0`. If absent, it can wait
five seconds for an `IONetworkController` match and then fall back to SysCfg.
0069 has82 generic match-timeout records followed by82 SysCfg/WiFi failures;
81 failures occur on the LaunchServices database gatherer. This is strong
correlated delay evidence, not proof that every generic match timeout has the
same caller or that this is the sole missing-home-screen cause.

0070 passes a verified device address through the existing BOOT-v2 header into
that six-byte DT property. It adds no WiFi network driver. The kernel emits only
`B2L: WiFi MAC metadata present, 6 bytes (address redacted)` or `absent`.

## Private input and packaging

The address is read from the target's EFS WiFi factory record. The temporary
recovery mount uses `ro,noload`, its flags are checked, and it is unmounted
immediately. The recovery network interface reports a different address, so
its value is not substituted for the factory record.

Keep the private input under ignored `out/private/`, directory mode0700 and
file mode0600. Package using `scripts/package-boot.py --wifi-mac-file` with a
private local file path. The actual address is never a command-line literal,
source constant, manifest field, or printed diagnostic. Personalized images and
archives contain the address and must stay local under ignored `out/`; they
must not be published. No actual device address is documented here.

The optional32-byte field after the256-byte seed contains `B2LWIFI1`, six raw
address bytes and18 zero bytes. The total header remains320 bytes and payload
version2. All-zero legacy metadata omits the property. Unknown/corrupt fields,
non-unicast addresses and an all-zero address are rejected. The seed, XNU
payload offset, firmware4MiB boundary and reserved memory layout remain intact.

## Verification

Host ASan/UBSan checks cover the C metadata decoder, legacy absence, malformed
fields and DT placement/length/capacity. All12 packaging tests pass, including
private-file parsing, malformed metadata and non-disclosure in verifier output.
The real XNU kernel and EFI/firmware builds pass; packaging confirms the exact
new loader is embedded in the firmware's compressed FV. A scan of all tracked
and nonignored files finds none of the target address's raw or textual forms.

The former temporary Mu workspace was absent. A persistent ignored build tree
was restored from Mu-Silicium2147dec93fc293ec85b1814a828816130d6f93b7 and its pinned
submodules, including Mu Basecore762a65dad79e2934cddbefa467b7e2c7b5ee4bc0, then
prepared with the reference board overlays and touch support. Symbols, source
receipt, personalized image and flash receipt remain local under ignored out/.

## Runtime checkpoint

Installation/readback is recorded separately in `out/0070/flash-receipt.json`.
Device runtime must confirm the redacted six-byte metadata marker, reduced
matching timeouts, LaunchServices database completion and subsequent UI progress.
The SpringBoard gSessionLock owner was not identified by0069's bounded capture;
a home-screen success claim requires a new observation.
