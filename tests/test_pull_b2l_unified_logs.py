#!/usr/bin/env python3
"""Synthetic HFS fixture: real linked-leaf parsing and file overflow resolution."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pull_logs', ROOT / 'scripts/pull-b2l-unified-logs.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
BS = 4096

def put16(b, off, value): struct.pack_into('>H', b, off, value)
def put32(b, off, value): struct.pack_into('>I', b, off, value)
def put64(b, off, value): struct.pack_into('>Q', b, off, value)
def fork(size, extents, total=None):
    b = bytearray(80); put64(b, 0, size); put32(b, 12, total if total is not None else sum(n for _, n in extents))
    for i, (start, count) in enumerate(extents): put32(b, 16 + 8*i, start); put32(b, 20 + 8*i, count)
    return b

def node(kind, height, records, forward=0, backward=0):
    b = bytearray(BS); put32(b, 0, forward); put32(b, 4, backward); b[8] = kind & 255; b[9] = height; put16(b, 10, len(records))
    off = 14
    for i, r in enumerate(records):
        put16(b, BS - 2*(i+1), off); b[off:off+len(r)] = r; off += len(r)
    put16(b, BS - 2*(len(records)+1), off)
    assert off <= BS - 2*(len(records)+1)
    return b

def header(depth, root, count, first, last, total):
    b = bytearray(106); put16(b, 0, depth); put32(b, 2, root); put32(b, 6, count); put32(b, 10, first); put32(b, 14, last); put16(b, 18, BS); put32(b, 22, total)
    return node(1, 0, [b, bytes(128), bytes(256)])

def extent_key(ident, start): return struct.pack('>HBBII', 10, 0, 0, ident, start)
def catkey(parent, name):
    text = name.encode('utf-16-be'); return struct.pack('>HIH', 6+len(text), parent, len(text)//2) + text

def folder(ident, parent, name):
    b=bytearray(88); put16(b, 0, 1); put32(b, 8, ident); return catkey(parent, name)+b

def file(ident, parent, name, f):
    b=bytearray(248); put16(b, 0, 2); put32(b, 8, ident); b[88:168]=f; return catkey(parent, name)+b

def fixture(path):
    data=bytearray(1024*BS)
    vh=bytearray(512); vh[:2]=b'H+'; put16(vh, 2, 4); put32(vh, 4, 0x100); put32(vh, 40, BS); put32(vh, 44, 1024)
    # Extents B-tree has an index node and a leaf carrying CNID10's ninth extent.
    ef=header(2,1,1,2,2,3)+node(0,2,[extent_key(10,8)+struct.pack('>I',2)])+node(-1,1,[extent_key(10,8)+fork(0,[(600,1)])[16:]])
    vh[192:272]=fork(len(ef),[(100,3)]); data[100*BS:103*BS]=ef
    records=[folder(2,1,'B2L'),folder(3,2,'private'),folder(4,3,'var'),folder(5,4,'db'),folder(6,5,'diagnostics'),folder(7,6,'Special'),folder(8,6,'Persist'),folder(9,6,'timesync'),folder(11,5,'uuidtext'),folder(12,11,'AA'),folder(17,4,'log')]
    records += [file(10,7,'0001.tracev3',fork(9*BS-7,[(500+2*i,1) for i in range(8)],9)),file(13,8,'0002.tracev3',fork(5,[(700,1)])),file(14,9,'0001.timesync',fork(4,[(710,1)])),file(15,12,'B'*30,fork(4,[(720,1)])),file(16,6,'version.plist',fork(4,[(730,1)])),file(18,17,'system.log',fork(4,[(740,1)])),file(19,6,'logd.0.log',fork(4,[(750,1)]))]
    cut=11
    cf=header(2,1,len(records),2,3,4)+node(0,2,[catkey(1,'B2L')+struct.pack('>I',2),catkey(6,'000')+struct.pack('>I',3)])+node(-1,1,records[:cut],forward=3)+node(-1,1,records[cut:],backward=2)
    vh[272:352]=fork(len(cf),[(200,4)]); data[200*BS:204*BS]=cf; data[1024:1536]=vh
    for i,b in enumerate([500+2*i for i in range(8)]+[600]): data[b*BS:(b+1)*BS]=bytes([65+i])*BS
    for block,text in [(700,b'trace'),(710,b'time'),(720,b'uuid'),(730,b'plst'),(740,b'logs'),(750,b'logd')]: data[block*BS:block*BS+len(text)]=text
    path.write_bytes(data)

class LogsTest(unittest.TestCase):
    def test_overflow_export_and_selection(self):
        with tempfile.TemporaryDirectory() as td:
            base=Path(td); image=base/'hfs.bin';fixture(image)
            with contextlib.redirect_stdout(io.StringIO()):r=m.export(m.ImageReader(image),base/'native.logarchive',('/private/var/log',))
            self.assertTrue(r['complete']); self.assertTrue(r['all_selected_complete']); self.assertTrue(r['header_unchanged'])
            out=base/'native.logarchive'; special=(out/'Special/0001.tracev3').read_bytes(); self.assertEqual(len(special),9*BS-7);self.assertEqual(special[8*BS:],b'I'*(BS-7))
            self.assertEqual((out/'Persist/0002.tracev3').read_bytes(),b'trace');self.assertEqual((out/'uuidtext/AA'/('B'*30)).read_bytes(),b'uuid')
            self.assertEqual((out/'supplemental/private/var/log/system.log').read_bytes(),b'logs');self.assertFalse((out/'private').exists())
            self.assertEqual(next(x for x in r['files'] if x['cnid']==10)['extents'],9)
            with self.assertRaises(FileExistsError):m.export(m.ImageReader(image),out)
    def test_bounds_and_short_reads(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'hfs.bin';fixture(p); reader=m.ImageReader(p)
            with self.assertRaises(m.ExportError):reader.read(reader.size,1)
            class ShortReader:
                size=reader.size; identity={}
                def read(self,off,size):return reader.read(off,max(0,size-1))
            with self.assertRaises(m.ExportError):m.Volume(ShortReader())
            volume=m.Volume(reader); raw=fork(BS,[(1024,1)])
            with self.assertRaises(m.ExportError):m.Fork(volume,raw,10).resolve(volume.overflow)
    def test_path_filtering(self):
        for name in ['../escape','/absolute','Persist/../escape','Persist//x','uuidtext/AA/../../x','a\\b','a\0b']:
            with self.assertRaises(m.ExportError):m.safe_relative(name)
        self.assertIsNone(m.archive_path('/private/var/db/other/private.tracev3'))
        self.assertIsNone(m.archive_path('/private/var/db/uuidtext/AA/not-a-uuid'))
        self.assertEqual(m.archive_path('/private/var/db/diagnostics/Special/a.tracev3'),('Special/a.tracev3',True))
    def test_btree_cycle_offset_and_missing_overflow(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'hfs.bin';fixture(p);data=bytearray(p.read_bytes())
            put32(data,203*BS,2);p.write_bytes(data)
            with self.assertRaises(m.ExportError):list(m.Volume(m.ImageReader(p)).selected())
            fixture(p);data=bytearray(p.read_bytes());put16(data,202*BS+BS-2,0);p.write_bytes(data)
            with self.assertRaises(m.ExportError):list(m.Volume(m.ImageReader(p)).selected())
            fixture(p);v=m.Volume(m.ImageReader(p));v.overflow.clear()
            e=next(e for e,_,_,_ in v.selected() if e['id']==10)
            with self.assertRaises(m.ExportError):m.Fork(v,e['fork_raw'],10).resolve(v.overflow)
    def test_changed_header_marks_incomplete(self):
        with tempfile.TemporaryDirectory() as td:
            base=Path(td);p=base/'hfs.bin';fixture(p);reader=m.ImageReader(p)
            class Changing:
                size=reader.size;identity={};headers=0
                def read(self,off,size):
                    b=reader.read(off,size)
                    if off==1024 and size==512:
                        self.headers+=1
                        if self.headers>1:b=b[:20]+bytes([b[20]^1])+b[21:]
                    return b
            with contextlib.redirect_stdout(io.StringIO()):r=m.export(Changing(),base/'changed.logarchive')
            self.assertFalse(r['complete']);self.assertTrue(any('header changed' in e for e in r['errors']))

if __name__=='__main__':unittest.main()
