# Repository source and privacy boundary

Commit project-authored source, tests, English documentation and patches against
publicly released open source dependencies. Preserve upstream license notices.
Model-level hardware constants and board definitions are necessary source;
identifiers unique to an individual unit are not repository content.

Keep the following outside Git, including commit messages and test fixtures:

- Phone serial numbers, IMEI, ECID, UDID, unique keys and raw device dumps.
- Private credentials, network addresses and user filesystem paths.
- BOOT backups, captures, full raw runtime logs and installation receipts.
- IPSWs, Apple firmware, dyld caches and proprietary frameworks or libraries.
- Apple SDK/KDK packages, non-public headers and binary dependencies.
- All generated kernels, EFI/FD images and packaged device images.

Local binary dependencies may be referenced by a documented version or path
placeholder. They must remain external build inputs, not copied into source.
Use synthetic identifiers in tests. Review the exact staged file list and diff
before every commit. The user's approved Git author identity is allowed.
