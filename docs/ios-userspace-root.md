# Preparing the development userspace root

Keep the IPSW, decrypted images, populated HFS image, hashes and installation
receipts under ignored local output directories. Do not publish Apple binaries,
activation records, personal data, device identifiers or partition captures.

The OS and both cryptex APFS images are mounted read-only on macOS with ownership
enabled. Copy ordinary uncompressed file data, numeric owners, permissions,
symlinks and hard links into a case-sensitive **Journaled HFS+** image. Preserve
symlink modes explicitly on macOS; regular `chmod` follows symlinks. Do not copy
APFS compression metadata onto decompressed file contents.

Place the SystemOS cryptex under `private/preboot/Cryptexes/OS`, and AppOS under
`private/preboot/Cryptexes/App`. The stock `System/Cryptexes` links resolve to
these directories. The preparation script mirrors missing cryptex counterparts
as hard links, retaining OS-root files when they already exist.

The current local root uses a sparse raw image with a 32 GiB filesystem. Its
logical file payload is approximately 20.28 GB with hard links preserved.
Filesystem metadata, journal and free working space are additional. Sparse host
allocation does not by itself make a raw ADB stream sparse: flash the complete
image, or use a writer that explicitly reconstructs zero ranges as well as data.
Never skip zero ranges onto an existing partition without reconstructing them.

Before writing a partition, cleanly unmount the image, check HFS, verify copied
file contents and metadata, and record the final image hash. Before a write,
independently validate the recovery identity, target alias, exact capacity and
unmounted state. A successful host image build does not prove device boot.

## Setup and activation preparation

After copying, run with ownership enabled and administrator privileges:

```sh
python3 scripts/prepare-ios-root.py /path/to/mounted/root --dry-run
python3 scripts/prepare-ios-root.py /path/to/mounted/root --report /private/local-report.json
python3 scripts/prepare-ios-root.py /path/to/mounted/root --verify
```

The script reads the product and build versions from the copied root. It writes
A5-derived Setup completion and migration preferences, prepares log/runtime
directories with the accounts in that root, and supplies cryptex fallback links.
It does not patch Apple executables or disable unrelated daemons.

Setup preferences do not themselves activate iOS. The EFI device tree supplies
the development-board `/chosen` tuple used by the native local Hactivation path:

| Property | Value |
| --- | ---: |
| `certificate-production-status` | 0 |
| `effective-production-status-ap` | 0 |
| `certificate-security-mode` | 1 |
| `effective-security-mode-sep` | 1 |

Static inspection of the supplied iOS 26.6.2 binary confirmed the inherited
selection path. Device runtime acceptance remains unverified. No Apple server
activation result or activation record is fabricated. If an actual existing MCM
container for mobileactivationd is present, the script can seed its state;
otherwise it reports that step skipped and does not invent container identities.

This prepares filesystem and boot-policy inputs. ARM64e instruction, ABI,
code-signing, kernel-service and graphics compatibility remain separate porting
requirements; preparation alone does not establish that this userspace runs.

See [native USERDATA root](userdata-root.md) for the selected partition's write
boundary and the `rd=b2l-userdata` alias.
