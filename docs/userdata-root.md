# Explicit native USERDATA root

Patch `0005-b2l-userdata-root.patch` adds the `rd=b2l-userdata` native root alias.
The BSD UFS overlay reads this exact boot argument before selecting a partition.
It requires one case-insensitive GPT `userdata` label and the existing valid
HFS+/HFSX volume-header geometry. Missing, duplicate or invalid USERDATA never
falls back to SYSTEM, super or another partition.

Without this exact alias, the existing SYSTEM selection remains: unique SYSTEM,
or unique super only when SYSTEM is absent. `rd=b2l-system` and explicit
canonical `b2lufs0pN` behavior remain available. The canonical node is derived
from the validated GPT entry index; no device layout or major number is fixed
in source. The IOKit native selector accepts both aliases and rejects an
explicit node that differs from the selected validated root.

Only the selected HFS partition receives a writable view and transport write
extent. When USERDATA is selected, SYSTEM, every other partition and the
whole-LU alias remain read-only. Existing byte-range, overflow, read/modify/write,
residual and flush behavior is unchanged. An operation through the whole-LU
alias cannot gain write access by addressing bytes inside the selected range.
The existing `b2l_root_rw=1` and `b2l_unsealed_root=1` development mount options
still apply to the selected validated HFS root.

This change does not create, copy or flash a filesystem image. A populated,
journaled HFS image and any proprietary firmware inputs remain private local
artifacts. No USERDATA backup, formatting or device operation is performed by
the tests below.

## Staged checks before applying the source change

```sh
python3 tests/test_ufs_disk.py /path/to/current/xnu --overlay
python3 tests/test_hfs_provider.py /path/to/current/xnu --pending
python3 tests/test_userdata_patch_sequence.py /path/to/current/xnu
```

The first check compiles actual overlay GPT/selector/strategy/write code under
ASan/UBSan with synthetic disks. Both SYSTEM and USERDATA contain valid HFS in
the selector fixture: the test verifies that only the requested view and extent
can write. It covers case-insensitive labels, no fallback, ambiguity, invalid
volume geometry, whole-LU aliases and out-of-range writes. The second applies
only the incremental tracked patch in a temporary directory and tests the actual
native alias branch plus the existing HFS mount policy. The final test applies
the real five-patch sequence in an isolated pinned Git fixture, checks upgrade
from four patches, verifies idempotence and rejects local edits.

After source preparation, omit `--overlay` and `--pending` to test the actual
prepared tree. The modified disk and IOKit translation units also compile with
the current native B2L ARM64 kernel flags. These checks establish local code
contracts and compilation, not a physical populated-root boot.
