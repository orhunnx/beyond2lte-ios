#!/usr/bin/env python3
"""Compile actual live XNU pager helpers against a contract-based VM model."""
import argparse,pathlib,re,subprocess,tempfile,hashlib,json,os,shutil,resource
p=argparse.ArgumentParser();p.add_argument('xnu',type=pathlib.Path);p.add_argument('--negative-shadow-regression',action='store_true');a=p.parse_args();here=pathlib.Path(__file__).resolve().parent
pager=a.xnu/'osfmk/vm/vm_shared_region_pager.c';slide=a.xnu/'osfmk/vm/vm_shared_region.c';ubc=a.xnu/'bsd/kern/ubc_subr.c'
def extract(text,name):
 m=re.search(r'(?m)^(?:static\s+)?(?:kern_return_t|boolean_t)\s*\n'+re.escape(name)+r'\(',text);assert m,name
 start=text.index('{',m.end());depth=1;i=start+1
 # Function bodies here have no braces hidden in string literals/comments which
 # upset balance; still strip comments and quoted strings for balanced scanning.
 tok=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
 for t in tok.finditer(text,start+1):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if not depth:return text[m.start():t.end()]
 raise AssertionError(name)
ps=pager.read_text();ss=slide.read_text();us=ubc.read_text()
# Admission is deliberately not modified or claimed as emulated by these tests.
assert re.search(r'if\s*\(\(cd->pageSize\s*!=\s*PAGE_SHIFT_4K\)\s*&&\s*\(cd->pageSize\s*!=\s*PAGE_SHIFT\)\)',us),'16K CodeDirectory admission guard changed: requires a separate review'
functions=[extract(ps,n) for n in ['b2l_shared_region_read_page','b2l_shared_region_chain_valid','b2l_shared_region_copy_large']]
sliders=[extract(ss,n) for n in ['vm_shared_region_slide_page_v3','vm_shared_region_slide_page_v5']]
with tempfile.TemporaryDirectory(prefix='b2l-pager-test-') as td:
 d=pathlib.Path(td);(d/'actual_helpers.h').write_text('\n\n'.join(functions)+'\n');(d/'actual_slide.h').write_text('\n\n'.join(sliders)+'\n')
 compiler=os.getenv('CLANG') or shutil.which('clang')
 if not compiler: raise SystemExit('clang is required')
 subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(d),str(here/'test_pager.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
 if a.negative_shadow_regression:
  # Recreate the reviewed old bug ONLY in the extracted temporary test copy.
  broken=functions[0].replace('FALSE, &local_info, NULL);','FALSE, info, NULL);')
  assert broken!=functions[0], 'Snapshot call shape changed; update negative test'
  (d/'actual_helpers.h').write_text('\n\n'.join([broken]+functions[1:])+'\n')
  subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-g','-fsanitize=address,undefined','-Wno-unused-but-set-variable','-I'+str(d),str(here/'test_pager.c'),'-o',str(d/'negative')],check=True)
  def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  r=subprocess.run([str(d/'negative')],capture_output=True,text=True,preexec_fn=no_core)
  assert r.returncode!=0 and 'info->lo_offset==expected_lo' in r.stderr, r.stderr
  print('Negative control: old mutable fault_info implementation fails fresh-shadow geometry assertion as expected.')
print('Actual helper hashes:',json.dumps({n:hashlib.sha256(f.encode()).hexdigest() for n,f in zip(['read_page','chain_valid','copy_large'],functions)}))
print('Original 4K/native CodeDirectory admission guard remains present; 16K signature validation is NOT covered or bypassed.')
