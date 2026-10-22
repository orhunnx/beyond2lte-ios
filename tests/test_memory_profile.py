#!/usr/bin/env python3
"""XNU RAM profile and bootstrap table budget, without device access."""
import importlib.util
import os
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare',ROOT/'scripts/prepare-boot-firmware.py')
prep=importlib.util.module_from_spec(spec);spec.loader.exec_module(prep)
OLD='  {"Upper bank guard", 0x980000000ULL, 0x080000000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},'
NEW='  {"B2L EL2 early memory", 0x9FD800000ULL, 0x02800000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},'
class Tests(unittest.TestCase):
    def test_only_budget_guard_changes(self):
        original='lowbank_and_firstupper_guard_unchanged\n'+OLD+'\nFD_stack_GOP_EL2_log_MMIO_unchanged\n'
        result=prep.xnu_memory_profile(original)
        self.assertEqual(result[:result.index('// B2L XNU RAM profile:')],original[:original.index(OLD)])
        self.assertTrue(result.endswith(NEW+'\nFD_stack_GOP_EL2_log_MMIO_unchanged\n'))
        self.assertEqual(result,prep.xnu_memory_profile(result))
        self.assertNotIn(OLD,result)
        for bad in ('',original.replace('0x080000000ULL','0x070000000ULL'),original+OLD):
            with self.assertRaises(ValueError): prep.xnu_memory_profile(bad)
    def test_real_external_source_if_supplied(self):
        source=os.environ.get('B2L_EXTERNAL_MEMORY_MAP')
        if not source: self.skipTest('optional external source audit')
        original=Path(source).read_text();result=prep.xnu_memory_profile(original)
        original_entries=re.findall(r'^\s*\{[^\n]+',original,re.M)
        result_entries=re.findall(r'^\s*\{[^\n]+',result,re.M)
        self.assertEqual([x for x in original_entries if '0x980000000ULL' not in x],
                         [x for x in result_entries if 'B2L EL2 early memory' not in x])
    def test_bootstrap_worst_case(self):
        board=(ROOT/'xnu-overlay/pexpert/pexpert/arm64/B2L.h').read_text()
        pages=int(re.search(r'B2L_BOOTSTRAP_TABLE_PAGES\s+(\d+)',board)[1])
        # start.s allocates2 roots, oneL2 per1GiB crossing in each mapping.
        # Its GOP mapping is20MiB and lies insideone1GiB slot (0xCA000000).
        # Enumerate every2MiB starting alignment of KVA and physical mappings.
        blocks=(4024*1024*1024)//(2*1024*1024)
        def l2_tables(offset): return (offset+blocks+511)//512
        worst=max(2+l2_tables(a)+l2_tables(b)+1 for a in range(512) for b in range(512))
        self.assertEqual(worst,13)
        self.assertGreaterEqual(pages,worst)
        self.assertLess(10,worst) # old allocation demonstrably insufficient
    def test_live_top_reservation_geometry(self):
        self.assertEqual(0x9FD800000+0x02800000,0xA00000000)
        self.assertEqual((0x9FD800000-0x900000000)//(1024*1024),4056)
        self.assertEqual(4056-32,4024)
        self.assertLess(0xA00000000,1<<40) # current XNU TCR IPS40
        self.assertGreater(0x900000000,1<<35) # 35physicalbits insufficient
if __name__=='__main__': unittest.main()
