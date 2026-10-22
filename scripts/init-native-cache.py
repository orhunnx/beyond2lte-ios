#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Create a new fixed native-cache file from a read-only root volume header.

Local files only. Does not format or access devices. Place the output in the
otherwise-unused journaled SYSTEM HFS volume and unmount it cleanly before use.
"""
import argparse, os, struct, zlib
from pathlib import Path
SIZE=128*1024*1024
COPY=12288

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--root-header',required=True,type=Path,help='First 4096 bytes of the intended HFS root')
 p.add_argument('--output',required=True,type=Path)
 a=p.parse_args();data=a.root_header.read_bytes()
 if len(data)!=4096 or data[1024:1028] not in (b'H+\0\4',b'HX\0\5'):p.error('expected first 4096 bytes of HFS+/HFSX root')
 h=bytearray(4096);h[:8]=b'B2LNC001'
 struct.pack_into('<IIII',h,8,1,SIZE,COPY,(SIZE-8192)//(2*COPY))
 h[24:36]=data[1128:1136]+data[1040:1044]
 struct.pack_into('<I',h,60,zlib.crc32(h[:60]))
 with a.output.open('xb') as f:
  f.write(h);f.write(h);zero=bytes(1024*1024);left=SIZE-8192
  while left:n=min(left,len(zero));f.write(zero[:n]);left-=n
  f.flush();os.fsync(f.fileno())
 print(f'Initialized {SIZE} bytes; matching headers; root identity bound; no device writes')
if __name__=='__main__':main()
