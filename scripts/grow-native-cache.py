#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Grow a cache on a LOCAL mounted SYSTEM image; preserve all original records.

Never opens block devices or flashes a phone. Retain the full source image too.
The target filesystem must have enough allocation space in at most eight inline extents. On error,
stop and recreate the staged image from its untouched full backup.
"""
import argparse,fcntl,json,os,stat,struct,zlib,shutil
from pathlib import Path
HEADER=8192;CELL=24576

def header(h):
 if len(h)!=4096 or zlib.crc32(h[:60])!=struct.unpack_from('<I',h,60)[0] or any(h[64:]):raise ValueError('invalid header checksum/reserved bytes')
 v,size,copy,slots=struct.unpack_from('<IIII',h,8)
 if copy!=12288:raise ValueError('unsupported record size')
 if v==1:
  if h[:8]!=b'B2LNC001' or size!=134217728 or any(h[36:60]):raise ValueError('invalid legacy header')
  used=slots;generation=0
 elif v==2:
  size,generation,used,reserved=struct.unpack_from('<QQII',h,36)
  if h[:8]!=b'B2LNC002' or struct.unpack_from('<I',h,12)[0] or not generation or reserved:raise ValueError('invalid v2 header')
 else:raise ValueError('unknown cache format')
 if size<HEADER+CELL or size%4096 or slots!=(size-HEADER)//CELL or used>slots:raise ValueError('invalid capacity')
 return dict(version=v,bytes=size,slots=slots,used=used,generation=generation,root=h[24:36])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cache',required=True,type=Path);p.add_argument('--backup',required=True,type=Path);p.add_argument('--bytes',required=True,type=int);p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
 st=a.cache.lstat()
 if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or str(a.cache.resolve()).startswith('/dev/'):p.error('expected regular local image file, no symlink/hardlink/device')
 with a.cache.open('rb') as f:hs=f.read(HEADER)
 candidates=[]
 for i in range(2):
  try:d=header(hs[i*4096:(i+1)*4096])
  except ValueError:continue
  if d['bytes']==st.st_size:candidates.append((d['generation'],i,d))
 if not candidates:p.error('no valid source header')
 _,_,d=max(candidates)
 if a.bytes<=d['bytes'] or a.bytes%4096 or (a.bytes-HEADER)//CELL>0xffffffff:p.error('invalid larger capacity')
 if a.backup.exists() or a.receipt.exists():p.error('backup/receipt already exists')
 with a.cache.open('rb') as src,a.backup.open('xb') as dst:
  shutil.copyfileobj(src,dst,8*1024*1024);dst.flush();os.fsync(dst.fileno())
 a.cache.unlink()
 with a.cache.open('xb') as f:
  # Darwin fstore_t: allocate the entire data fork, or fail; the validated HFS view maps up to eight extents.
  fcntl.fcntl(f.fileno(),42,struct.pack('=IIqqq',4,3,0,a.bytes,0))
  f.truncate(a.bytes)
  h=bytearray(4096);h[:8]=b'B2LNC002';struct.pack_into('<IIII',h,8,2,0,12288,(a.bytes-HEADER)//CELL)
  h[24:36]=d['root'];struct.pack_into('<QQII',h,36,a.bytes,1,d['used'],0);struct.pack_into('<I',h,60,zlib.crc32(h[:60]))
  f.write(h);f.write(h)
  with a.backup.open('rb') as old:
   old.seek(HEADER);shutil.copyfileobj(old,f,8*1024*1024)
  # Explicit zeros are needed because firmware reads raw allocated extents.
  zero=bytes(16*1024*1024);left=a.bytes-d['bytes']
  while left:n=min(left,len(zero));f.write(zero[:n]);left-=n
  f.flush();os.fsync(f.fileno())
 a.receipt.write_text(json.dumps(dict(old_bytes=d['bytes'],new_bytes=a.bytes,old_slots=d['slots'],new_slots=(a.bytes-HEADER)//CELL,committed_used=d['used'],records_preserved=True,local_files_only=True),indent=2)+'\n')
 print(a.receipt.read_text())
if __name__=='__main__':main()
