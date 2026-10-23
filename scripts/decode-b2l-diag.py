#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Reassemble bounded B2L IPC captures. Does not assume a private XPC schema.

Raw bytes and printable tokens are evidence; message IDs/ports alone do not
establish cross-process request/reply identity. Keep output local to out/.
"""
import argparse,json,re
from pathlib import Path

def decode(text):
    messages={};parts={};waits=[];errors=[];delivery=[];rights=[];progress=[];mach_provenance=[]
    for line in text.splitlines():
        m=re.search(r'B2L IPC: seq=(\d+) (.*)',line)
        if m:
            seq=int(m[1]);fields=dict(re.findall(r'(\w+)=([^ ]+)',m[2]));messages[seq]=dict(sequence=seq,fields=fields,parts={})
        m=re.search(r'B2L AMFI: seq=(\d+) (.*)',line)
        if m:
            seq=int(m[1]);fields=dict(re.findall(r'(\w+)=([^ ]+)',m[2]));fields['kind']='amfi_constraint'
            messages[seq]=dict(sequence=seq,fields=fields,parts={})
        m=re.search(r'B2L IPCDATA: seq=(\d+) part=(\w+) (.*)',line)
        if m:
            seq=int(m[1]);p=parts.setdefault((seq,m[2]),dict(chunks={},copy_errors=[],capture_limit=None))
            fields=dict(re.findall(r'(\w+)=([^ ]+)',m[3]))
            if 'captured' in fields:p['capture_limit']=int(fields['captured'])
            if 'hex' in fields:
                try:p['chunks'][int(fields['off'])]=bytes.fromhex(fields['hex'])
                except (ValueError,KeyError):p['copy_errors'].append('invalid hex record')
            if 'copyin_error' in fields:p['copy_errors'].append(int(fields.get('off',0)))
        if 'B2L PROGRESS:' in line or 'B2L PROGRESS SUMMARY:' in line:progress.append(line)
        if 'B2L MACH CONSTRUCT:' in line or 'B2L MACH VALIDATE:' in line:mach_provenance.append(line)
        if 'B2L IPCRCV:' in line or 'B2L IPCPORT:' in line:delivery.append(line)
        if 'B2L SONCE:' in line or 'B2L SONCEKBT:' in line or 'B2L SONCEUBT:' in line:rights.append(line)
        if 'B2L WAIT:' in line or 'B2L WAITBT:' in line or 'B2L IOKITWAIT:' in line:waits.append(line)
        if 'B2L SYSERR:' in line or 'B2L SYSPATH:' in line or 'B2L CSRESULT:' in line:errors.append(line)
    for (seq,kind),p in parts.items():
        target=messages.setdefault(seq,dict(sequence=seq,fields={},parts={}))
        cursor=0;data=bytearray();gaps=[]
        for off,chunk in sorted(p['chunks'].items()):
            if off!=cursor:gaps.append([cursor,off])
            data.extend(chunk);cursor=off+len(chunk)
        if p['capture_limit'] is not None and cursor!=p['capture_limit']:gaps.append([cursor,p['capture_limit']])
        target['parts'][kind]=dict(captured_bytes=len(data),capture_limit=p['capture_limit'],
            gaps=gaps,copy_errors=p['copy_errors'],hex=data.hex(),
            printable_tokens=[s.decode('ascii') for s in re.findall(rb'[ -~]{4,}',data)])
    return dict(messages=[messages[k] for k in sorted(messages)],wait_records=waits,syscall_errors=errors,delivery_records=delivery,right_lifetime_records=rights,progress_records=progress,mach_provenance_records=mach_provenance,
        limitations='Payloads are bounded prefixes; gaps/copy errors are explicit. Printable tokens are not decoded XPC semantics.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    result=decode(a.input.read_text(errors='replace'))
    with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps(dict(messages=len(result['messages']),wait_records=len(result['wait_records']),syscall_errors=len(result['syscall_errors']))))
if __name__=='__main__':main()
