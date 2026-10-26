#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build our bounded native iOS migration launcher and its kernel seed header."""
from pathlib import Path
from b2l_launch_cache import pad_binary_plist
import argparse,hashlib,json,plistlib,subprocess,tempfile,importlib.util,struct
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
with tempfile.TemporaryDirectory(prefix='b2l-app-bootstrap-') as td:
 binary=Path(td)/'b2l-system-app-bootstrap-v2'
 subprocess.run(['xcrun','--sdk','iphoneos','clang','-target','arm64e-apple-ios26.0','-isysroot',sdk,'-Os','-fobjc-arc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-framework','Foundation',str(r/'userspace/system-app-bootstrap.m'),'-o',str(binary)],check=True)
 subprocess.run(['/usr/bin/codesign','--force','--sign','-','--identifier','dev.b2l.system-app-bootstrap','--entitlements',str(r/'userspace/system-app-bootstrap.entitlements.plist'),'--generate-entitlement-der',str(binary)],check=True)
 subprocess.run(['/usr/bin/codesign','--verify',str(binary)],check=True)
 data=binary.read_bytes()
 cputype,subtype=struct.unpack_from('<II',data,4)
 if cputype!=0x100000c or subtype&0xffffff!=2 or not subtype&0x80000000:
  raise SystemExit('Bootstrap must carry the arm64e pointer-auth ABI subtype')
 if len(data)>512*1024:raise SystemExit('Bootstrap exceeds bounded payload size')
 job=plistlib.dumps({'Label':'dev.b2l.system-app-bootstrap-v2','ProgramArguments':['/usr/local/libexec/b2l-system-app-bootstrap-v2'],'RunAtLoad':True,'UserName':'root','ProcessType':'Background','LowPriorityIO':True,'StandardOutPath':'/private/var/log/b2l-system-app-bootstrap-v2.log','StandardErrorPath':'/private/var/log/b2l-system-app-bootstrap-v2.log'},fmt=plistlib.FMT_XML)
 # Read the exact source launch cache from the local prepared IPSW image.
 # Only the private generated header contains Apple cache data.
 spec=importlib.util.spec_from_file_location('hfsreader',r/'scripts/pull-b2l-unified-logs.py')
 m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 reader=m.ImageReader(r/'out/ipsw/ios26-userdata.hfs.img');volume=m.Volume(reader)
 cache_path='/System/Library/xpc/launchd.plist';source=None
 for entry,full,_,_ in volume.selected((cache_path,)):
  if full==cache_path:source=b''.join(m.Fork(volume,entry['fork_raw'],entry['id']).resolve(volume.overflow).content());break
 if source is None or not volume.unchanged():raise SystemExit('Missing/changed original launch cache')
 reader.close()
 original=plistlib.loads(source);derived=plistlib.loads(source)
 job_path='/System/Library/LaunchDaemons/dev.b2l.system-app-bootstrap-v2.plist'
 if job_path in derived['LaunchDaemons']:raise SystemExit('Owned job already in source launch cache')
 derived['LaunchDaemons'][job_path]=plistlib.loads(job)
 check=plistlib.loads(plistlib.dumps(derived,fmt=plistlib.FMT_BINARY));del check['LaunchDaemons'][job_path]
 if check!=original:raise SystemExit('Original launch cache entries changed')
 cache=pad_binary_plist(plistlib.dumps(derived,fmt=plistlib.FMT_BINARY,sort_keys=True),len(source))
 if len(cache)>4*1024*1024:raise SystemExit('Derived launch cache exceeds 4 MiB')
 a.output.parent.mkdir(parents=True,exist_ok=True)
 def array(name,b):return 'static const unsigned char '+name+'[] = {\n'+''.join(' '+','.join('0x%02x'%x for x in b[i:i+20])+',\n' for i in range(0,len(b),20))+'};\n'
 a.output.write_text('/* Private generated owned launcher and local source-image cache payload. */\n'+array('b2l_apps_executable',data)+array('b2l_apps_job',job)+array('b2l_apps_launch_cache',cache))
 (a.output.parent/'b2l-userspace-payload.json').write_text(json.dumps({'binary_arch':'arm64e','binary_cpusubtype':subtype,'binary_bytes':len(data),'binary_sha256':hashlib.sha256(data).hexdigest(),'job_sha256':hashlib.sha256(job).hexdigest(),'original_cache_sha256':hashlib.sha256(source).hexdigest(),'original_cache_bytes':len(source),'shared_handoff_size_preserved':len(source)==len(cache),'derived_cache_bytes':len(cache),'derived_cache_sha256':hashlib.sha256(cache).hexdigest(),'original_cache_entries_preserved':True,'added_job':job_path},indent=2)+'\n')
 print('Built native arm64e bootstrap:',len(data),'bytes; sha256',hashlib.sha256(data).hexdigest())
