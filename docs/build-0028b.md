# Build 0028b: register policies before lockdown

The 0028 device panic resolves to `mac_policy_register`, writing
`mac_policy_list.freehint`. On this iOS XNU configuration the registry and
static entries are SECURITY_READ_ONLY_LATE. Our BSD-time registration happened
after machine_lockdown and faulted on protected kernel static memory.

Patch 0018 removes both policy registrations from bsd_init and places them in
mac_policy_initmach, after normal security extensions and before mac_late.
The AMFI interface is already registered at EARLY_BOOT; lockdown occurs later.
The policy behavior, firmware and memory protections are unchanged.

The eighteen-patch sequence and actual startup ordering regression checks pass.
Full native kernel/package validation, matching kernel/dSYM UUIDs and complete
BOOT readback passed. The corrected BOOT was installed and reboot requested. The next device screen must establish whether policy
registration now completes and launchd reaches its previous startup stage.
