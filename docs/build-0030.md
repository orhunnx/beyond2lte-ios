# Build 0030: volatile development system keybag

Adds the gated AppleKeyStore IOResources provider described in
[development KeyStore](development-keystore.md). The current root remains the
existing journaled HFS USERDATA volume. No root image or filesystem placeholder
bag is rewritten. The known EFI/FD and boot arguments are reused.

## Validation and installation

Actual-source dispatcher, adapter and complete service/client lifecycle tests
pass under ASan/UBSan. The twenty-patch sequence passes the 0019-to-0020 upgrade,
idempotence and local-edit refusal tests. Both native translation units and the
full kernel build pass. Kernel inspection and BOOT package verification pass.

The installed BOOT is 23,113,616 bytes. Its complete prefix was read back from
the device and matched SHA-256
`a49dabeec3e91076162d504822a32e6752cdb757bffabcbc6124d7e9d74343c0`.
Stripped kernel, unstripped kernel and preserved dSYM share UUID
`58DE600F-3B92-380B-9C61-19032320061F`. Reboot was requested after verification.

Runtime success is pending the next physical screen: service registration,
successful selector 0/14 queries, and keybagd passing its previous fatal
system-bag initialization. Later cryptographic operations remain unsupported.
