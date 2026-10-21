# Mixed shared-cache slide pager regression harness

Run:

```sh
python3 run.py /path/to/prepared/xnu --negative-shadow-regression
```

Set `CLANG` to select a compiler; otherwise `clang` is found in PATH. A C11 compiler with ASan/UBSan and Python3 are required. This test uses synthetic data only. It reads the specified prepared source tree and makes no changes to it. Temporary extracted source and binaries are removed when the runner exits.

The runner extracts these **actual current source functions**, rather than duplicating their implementation:

- `b2l_shared_region_read_page`
- `b2l_shared_region_chain_valid`
- `b2l_shared_region_copy_large`
- `vm_shared_region_slide_page_v3`
- `vm_shared_region_slide_page_v5`

The narrow dispatch stub selects the actual V3/V5 implementation. The VM model follows `vm_fault_page`'s documented consumed-lock/paging-reference contract. Success returns a locked busy page/owner paging reference, optionally with a top page plus original-object paging reference. Ordinary error/retry consumes the donated reference/lock; SUCCESS_NO_VM_PAGE retains them. Assertions check every transition and balanced release. Host memory allocations model separate physical pages with deliberately unrelated synthetic physical page numbers, so a contiguous16KiB physical copy would fail ASan.

Covered cases:

- All four requested4KiB slices for V3 and V5, with/without a shadow owner.
- A non16KiB-aligned but4KiB-aligned nonzero slide start (`0x23000`) and chains crossing every4KiB boundary, ending at the last valid8-byte slot.
- The actual slide result, authenticated and unauthenticated raw targets, original source preservation, and4KiB-only destination writes.
- AND of validation bits, OR of all input taint, target-page NX, signed/unsigned objects and validation callback contract.
- Every modeled VM fault status; retry, memory-wait success/interruption, top-page cleanup and fresh shadow-adjusted fault-info snapshots.
- Failure at each of the four source reads, unchanged destination/flags on failure, allocation shortage, disabled compatibility, partial16KiB ranges, unaligned/reversed ranges, page-index overflow, page-start count overflow, unsupported versions, absent chains and raw copy outside the sliding range.
- Malformed chain starts, jumps beyond scratch and V3 bind records rejected before actual slide mutation.
- Optional negative control deliberately restores the reviewed mutable-info bug in a **temporary extracted copy** and verifies the fresh-shadow assertion fails. It never edits the kernel source.

The runner also asserts that the existing4KiB/native CodeDirectory page-size admission guard remains present in `ubc_subr.c`. This harness does **not** emulate, bypass or prove16KiB code-signature validation on a4KiB VM system. The code-signature callback is a lock/address/size contract stub; hashing and signature admission are separate gates.

This exercises helper code with a VM contract model. It does not run the real VM scheduler, vnode pager, code-signing implementation or outer UPL commit/abort path and does not establish on-device boot success. The outer UPL index/physical lookup path still requires native integration/runtime verification.
