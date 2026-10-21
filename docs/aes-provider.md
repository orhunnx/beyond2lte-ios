# Portable AES dependency

The B2L legacy AES CBC/ECB adapter uses BearSSL's MIT-licensed portable
bitsliced `aes_ct` implementation. It follows the A5 port's public `0020` and
`0098` integration pattern, with current XNU context assertions and explicit
unavailable-GCM handling. It is a real cipher implementation, not an empty
corecrypto registration table.

- Official source: [BearSSL repository](https://www.bearssl.org/git/BearSSL).
- Immutable commit: `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`.
- Algorithm design: [BearSSL constant-time AES](https://bearssl.org/constanttime.html).
- Test vectors: [NIST SP 800-38A](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf), Appendix F.1/F.2.

## Staging and build

`scripts/build-kernel.sh` runs `scripts/stage-bearssl.py` before XNU's standalone
build target. First use fetches the pinned source into ignored
`out/dependencies/bearssl`. The stage contains the five `aes_ct` implementation
files, required public headers and `LICENSE.txt`, under the local XNU checkout's
`libkern/crypto/b2l_bearssl` directory. Only `inner.h`'s limits-header selection
is adapted for XNU's kernel include environment. Cipher code stays upstream.

An existing clean checkout can avoid another fetch:

```sh
BEARSSL_SOURCE=/path/to/pinned/BearSSL \
  KDKROOT=/path/to/merged-kdk B2L_DEPSROOT=/path/to/deps \
  scripts/build-kernel.sh CWARNFLAGS=-w CXXWARNFLAGS=-w
```

The script verifies both source pins and a hash manifest, refuses edited or
unmanaged staging, and does not reset user checkouts. Fetched dependency sources
and generated artifacts are external local inputs, not committed overlay files.
No storage partition, firmware MMIO or physical RAM guard changes are involved.

The adapter and cipher compile with `-mgeneral-regs-only`, require no heap state,
and implement AES-128/192/256 CBC and legacy single-block ECB. GCM remains an
explicit unsupported service when the external provider is absent/incomplete.
No claim of whole-system timing resistance or cryptographic module validation
is made by passing implementation vectors.

```sh
python3 tests/test_aes_provider.py /path/to/patched/xnu
```

This test uses actual cipher/adapter/wrapper code under ASan/UBSan and checks
NIST known answers, key-unit compatibility, copying, buffer/IV semantics and
negative unsupported-GCM paths. It does not emulate the target's TCP network
stack or prove physical-device execution.
