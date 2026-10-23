#!/usr/bin/env python3
"""Actual patched native construction branch + actual provider ownership tests."""
import ast
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
xnu=Path(os.environ.get('XNU_PATH',root/'out/build-env-0063/src/xnu'))
module=ast.parse((root/'tests/test-mach-filter.py').read_text())
prefix=next(ast.literal_eval(n.value) for n in module.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='prefix' for t in n.targets))
with tempfile.TemporaryDirectory(prefix='b2l-anon-native-') as td:
 d=Path(td);target=d/'osfmk/ipc/mach_port.c';target.parent.mkdir(parents=True);shutil.copyfile(xnu/'osfmk/ipc/mach_port.c',target)
 if 'kr = b2l_mach_filter_anonymous_connection(options->flags,' not in target.read_text():
  subprocess.run(['patch','--batch','--silent','-p1','-d',str(d),'-i',str(root/'patches/0046-b2l-anonymous-forward-connection-label.patch')],check=True)
 source=target.read_text();construct=source[source.index('mach_port_construct(\n'):];start=construct.index('\tif (ip_is_any_service_port_type(label.io_type)) {\n\t\tkr = ipc_service_port_label_alloc')
 end=construct.index('\t/* Port locked and active */',start);branch=construct[start:end]
 (d/'include/mach').mkdir(parents=True);(d/'include/mach/kern_return.h').write_text('typedef int kern_return_t;\n')
 mock=r'''
#define ARM64_BOARD_CONFIG_B2L 1
#define CONFIG_MACF 1
#define IOT_CONNECTION_PORT 5
struct ipc_label {int io_type;void *iol_connection;bool io_filtered;};
static struct ipc_label captured;
static unsigned port_allocations;
static bool port_fail;
static void *named_parent;
static struct mach_service_port_info info;
struct fixture_options {uint32_t flags,service_port_name;};
static int ip_is_any_service_port_type(int type){return type==2;}
static int ip_is_bootstrap_port_type(int type){return type==3;}
static int ipc_service_port_label_alloc(void *info_arg,struct ipc_label *label){(void)info_arg;(void)label;return KERN_FAILURE;}
static int ipc_bootstrap_port_label_alloc(void *info_arg,struct ipc_label *label){(void)info_arg;(void)label;return KERN_FAILURE;}
static int ipc_service_port_derive_sblabel(uint32_t parent,struct ipc_label *label,uint32_t flags){assert(parent!=MPO_ANONYMOUS_SERVICE);if(!(flags&MPO_IMMOVABLE_RECEIVE)){boolean_t filter=1;b2l_mach_filter_retain(named_parent);label->iol_connection=b2l_mach_filter_derive(named_parent,&filter);}return 0;}
/* Native ipc_port_alloc transfers ownership and frees the label on failure. */
static int ipc_port_alloc(int space,struct ipc_label label,int init,unsigned *name,void **port){assert(space==1&&!init);port_allocations++;captured=label;if(port_fail){b2l_mach_filter_release(label.iol_connection);captured.iol_connection=NULL;return KERN_RESOURCE_SHORTAGE;}*name=22;*port=(void*)1;return 0;}
static int construct_native(int type,uint32_t flags,uint32_t parent){
 struct fixture_options storage={flags,parent},*options=&storage;struct ipc_label label={type,NULL,false};int kr=0,space=1,init_flags=0;unsigned name_value=0,*name=&name_value;void *port=NULL;struct mach_service_port_info sp_info={0};
'''
 test=r'''
 return KERN_SUCCESS;
}
static void clean(void){b2l_mach_filter_release(captured.iol_connection);captured.iol_connection=NULL;}
int main(void){
 assert(!b2l_mach_filter_register());assert(!b2l_mach_filter_alloc(&info,&named_parent));
 unsigned before=port_allocations;assert(!construct_native(5,0x283f,MPO_ANONYMOUS_SERVICE));
 assert(port_allocations==before+1&&captured.iol_connection&&!captured.io_filtered);
 assert(((struct b2l_mach_service_label*)captured.iol_connection)->kind==B2L_MACH_ANONYMOUS_FORWARD);clean();
 assert(!construct_native(5,0x283f|MPO_FILTER_MSG,MPO_ANONYMOUS_SERVICE)&&captured.iol_connection&&captured.io_filtered);clean();
 assert(!construct_native(5,0x283f|MPO_IMMOVABLE_RECEIVE,MPO_ANONYMOUS_SERVICE)&&!captured.iol_connection&&!captured.io_filtered);
 assert(!construct_native(0,0,MPO_ANONYMOUS_SERVICE)&&!captured.iol_connection);
 assert(!construct_native(5,0x283f,123)&&captured.iol_connection==named_parent);clean();
 fail_alloc=true;before=port_allocations;assert(construct_native(5,0x283f,MPO_ANONYMOUS_SERVICE)==KERN_RESOURCE_SHORTAGE&&port_allocations==before);fail_alloc=false;
 unsigned allocated=atomic_load(&allocations),freed=atomic_load(&frees);port_fail=true;assert(construct_native(5,0x283f,MPO_ANONYMOUS_SERVICE)==KERN_RESOURCE_SHORTAGE);port_fail=false;
 assert(atomic_load(&allocations)==allocated+1&&atomic_load(&frees)==freed+1&&!captured.iol_connection);
 enabled=false;assert(!construct_native(5,0x283f|MPO_FILTER_MSG,MPO_ANONYMOUS_SERVICE)&&!captured.iol_connection&&!captured.io_filtered);enabled=true;
 b2l_mach_filter_owned=false;assert(!construct_native(5,0x283f|MPO_FILTER_MSG,MPO_ANONYMOUS_SERVICE)&&!captured.iol_connection&&!captured.io_filtered);
 b2l_mach_filter_release(named_parent);assert(atomic_load(&allocations)==atomic_load(&frees));
 puts("Native anonymous constructor: typed forward only, backward/wrong type exclusion, named derive unchanged, inactive pass-through, filtering and allocation cleanup passed");
}
'''
 body=prefix+'\n#include "'+str(root/'xnu-overlay/bsd/kern/b2l_mach_filter.h')+'"\n'+mock+branch+test
 (d/'test.c').write_text(body);cc=os.getenv('CLANG') or shutil.which('clang');assert cc
 subprocess.run([cc,'-I'+str(d/'include'),'-I'+str(root/'xnu-overlay/osfmk'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
