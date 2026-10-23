#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compatibility entry point for the expanded actual 0x66 callback tests.

0063 adds genuine entitlement-key existence evaluation; unavailable-only cases
remain part of test-lwcr.py together with match/nonmatch, lifetime and ABI tests.
"""
import pathlib
import runpy
runpy.run_path(str(pathlib.Path(__file__).with_name('test-lwcr.py')), run_name='__main__')
