#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the real pinned startup graph for policy-list lockdown ordering."""
from pathlib import Path
import sys
x=Path(sys.argv[1]);base=(x/'security/mac_base.c').read_text()
start=(x/'osfmk/kern/startup.c').read_text();bsd=(x/'bsd/kern/bsd_init.c').read_text()
phase=base.split('mac_policy_initmach(void)\n{',1)[1].split('\n}\n',1)[0]
assert phase.index('load_security_extensions_function();') < phase.index('b2l_sandbox_register();') < phase.index('b2l_amfi_policy_register();') < phase.index('mac_late = 1;')
assert '#if defined(ARM64_BOARD_CONFIG_B2L)' in phase
for call in ['b2l_sandbox_register();','b2l_amfi_policy_register();']:
 assert base.count(call)==1 and call not in bsd
assert start.index('mac_policy_init();') < start.index('kernel_startup_initialize_upto(STARTUP_SUB_EARLY_BOOT);') < start.index('mac_policy_initmach();') < start.index('kernel_startup_initialize_upto(STARTUP_SUB_LOCKDOWN);') < start.index('machine_lockdown();') < start.index('bsd_init();')
assert 'SECURITY_READ_ONLY_LATE(mac_policy_list_t) mac_policy_list;' in base
print('Actual startup graph: AMFI interface ready, both policies before MAC late/lockdown, no BSD-time registration PASS')
