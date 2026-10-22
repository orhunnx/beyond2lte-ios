#!/usr/bin/env python3
"""Initialize/decode the original B2L framed raw console region. No device IO."""
import argparse,json,struct,zlib
from pathlib import Path
PAGE=4096
REGION=16*1024*1024
DATA_OFFSET=2*PAGE
SPAN=3584
MAGIC=b'B2LLOG01'
RECORD_MAGIC=b'B2LLOGPG'
def crc(page):
    q=bytearray(page);q[48:52]=bytes(4);return zlib.crc32(q)&0xffffffff

def header(size=REGION,epoch=0):
    if size!=REGION:raise ValueError('region must be exactly 16 MiB')
    q=bytearray(PAGE)
    struct.pack_into('<8sIIQIIQQ',q,0,MAGIC,1,PAGE,size,PAGE,(size-DATA_OFFSET)//PAGE,DATA_OFFSET,epoch)
    struct.pack_into('<I',q,48,crc(q))
    return bytes(q)
def parse_header(q):
    if len(q)!=PAGE:raise ValueError('short header')
    magic,ver,hbytes,size,rbytes,count,offset,epoch=struct.unpack_from('<8sIIQIIQQ',q)
    if(magic,ver,hbytes,size,rbytes,count,offset)!=(MAGIC,1,PAGE,REGION,PAGE,(REGION-DATA_OFFSET)//PAGE,DATA_OFFSET):raise ValueError('header geometry/magic')
    if struct.unpack_from('<I',q,48)[0]!=crc(q):raise ValueError('header CRC')
    if any(q[52:]):raise ValueError('unknown header fields')
    return {'size':size,'count':count,'epoch':epoch}
def parse_record(q,epoch):
    if len(q)!=PAGE:raise ValueError('short record')
    magic,ver,hbytes,got_epoch,index,first,span,valid,check,flags,drops=struct.unpack_from('<8sIIQQQIIIIQ',q)
    if(magic,ver,hbytes,got_epoch)!=(RECORD_MAGIC,1,64,epoch):raise ValueError('record metadata')
    if not 1<=span<=SPAN or flags or valid>span or check!=crc(q):raise ValueError('record bounds/CRC')
    present=[bool(q[64+i//8]&(1<<(i%8))) for i in range(SPAN)]
    if sum(present[:span])!=valid or any(present[span:]) or first+span>((1<<56)-1):raise ValueError('record bitmap/sequence')
    return {'index':index,'first':first,'span':span,'valid':valid,'drops':drops,'present':present,'payload':q[512:]}
def main():
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='cmd',required=True)
    ini=sub.add_parser('init');ini.add_argument('output',type=Path)
    dec=sub.add_parser('decode');dec.add_argument('input',type=Path);dec.add_argument('--offset',type=lambda x:int(x,0),default=0);dec.add_argument('--text',type=Path)
    a=ap.parse_args()
    if a.cmd=='init':
        with a.output.open('xb') as f:
            f.truncate(REGION);f.seek(0);f.write(header());f.write(header());f.flush()
        print(json.dumps({'size':REGION,'headers':[0,PAGE],'record_offset':DATA_OFFSET,'record_count':(REGION-DATA_OFFSET)//PAGE,'page_bytes':PAGE}));return
    with a.input.open('rb') as f:
        hs=[]
        for i in range(2):
            f.seek(a.offset+i*PAGE)
            try:hs.append((parse_header(f.read(PAGE)),i))
            except ValueError:pass
        if not hs:raise SystemExit('no valid region header')
        h,slot=max(hs,key=lambda x:(x[0]['epoch'],-x[1]));records=[];bad=0
        for i in range(h['count']):
            f.seek(a.offset+DATA_OFFSET+i*PAGE);q=f.read(PAGE)
            try:
                r=parse_record(q,h['epoch'])
                if r['index']%h['count']!=i:raise ValueError('misplaced record')
                records.append(r)
            except ValueError:bad+=1
    records.sort(key=lambda r:r['index']);text=bytearray();last=None;missing=0
    for r in records:
        if last is None and r['first']:
            text.extend(b'\n[B2L LOG: earlier console bytes unavailable]\n')
        if last is not None and r['first']!=last:text.extend(b'\n[B2L LOG: sequence gap]\n')
        hole=0
        for i in range(r['span']):
            if r['present'][i]:
                if hole:text.extend(('\n[B2L LOG: %d missing byte positions]\n'%hole).encode());hole=0
                text.append(r['payload'][i])
            else:missing+=1;hole+=1
        if hole:text.extend(('\n[B2L LOG: %d missing byte positions]\n'%hole).encode())
        last=r['first']+r['span']
    if a.text:a.text.write_bytes(text)
    print(json.dumps({'epoch':h['epoch'],'header_slot':slot,'valid_records':len(records),'ignored_pages':bad,'missing_positions':missing,'first_sequence':records[0]['first'] if records else None,'end_sequence':last,'producer_drops':records[-1]['drops'] if records else None,'text_bytes':len(text)}))
if __name__=='__main__':main()
