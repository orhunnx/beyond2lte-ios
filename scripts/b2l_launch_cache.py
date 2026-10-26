# SPDX-License-Identifier: BSD-2-Clause
"""Preserve launchd's original shared-memory cache length during child handoff."""
import plistlib,struct
MAX_CACHE_BYTES=4*1024*1024

def pad_binary_plist(data,expected_bytes):
    if not data.startswith(b'bplist00') or len(data)<40:
        raise ValueError('Not a binary plist')
    if not len(data)<=expected_bytes<=MAX_CACHE_BYTES:
        raise ValueError('Derived cache does not fit original handoff length')
    trailer=data[-32:];offset_size,ref_size=trailer[6:8]
    objects,top,offset=struct.unpack('>QQQ',trailer[8:])
    if offset_size not in (1,2,4,8) or ref_size not in (1,2,4,8) or not 0<=top<objects or not 8<=offset<=len(data)-32 or offset+objects*offset_size>len(data)-32:
        raise ValueError('Invalid binary plist offset table')
    padding=expected_bytes-len(data)
    # Object bytes and their offsets stay unchanged. Move only the offset table
    # and trailer so the trailer remains at the end of the shared-memory view.
    result=data[:offset]+b'\0'*padding+data[offset:-32]+trailer[:24]+struct.pack('>Q',offset+padding)
    if len(result)!=expected_bytes or plistlib.loads(result)!=plistlib.loads(data):
        raise ValueError('Padded cache changed plist semantics')
    return result
