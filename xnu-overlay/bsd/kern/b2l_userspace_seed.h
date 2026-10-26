/* SPDX-License-Identifier: BSD-2-Clause
 * Opt-in publication of the owned native migration launcher. Existing files
 * must match exactly; a synced executable is published before its launch job.
 */
#include <pexpert/pexpert.h>
#include "b2l_userspace_payload.h"
#include "b2l_apps_payload_compare.h"
static int b2l_apps_cache_ready;
int b2l_apps_cache_is_ready(void){return b2l_apps_cache_ready;}
static int b2l_apps_directory(const char *path,vfs_context_t ctx)
{
    struct nameidata nd;struct vnode_attr va;vnode_t vp=NULL;
    NDINIT(&nd,CREATE,OP_MKDIR,LOCKPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(path),ctx);
    nd.ni_flag|=NAMEI_NOFOLLOW_ANY;nd.ni_cnd.cn_flags|=WILLBEDIR;
    int error=namei(&nd);if(error)return error;
    if(!root_type(nd.ni_dvp,VDIR)){error=EXDEV;goto out;}
    if(nd.ni_vp){
        VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);
        error=root_type(nd.ni_vp,VDIR)?vnode_getattr(nd.ni_vp,&va,ctx):ENOTDIR;
        if(!error&&(!VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_mode)||va.va_uid||(va.va_mode&0022)))error=EPERM;
        goto out;
    }
    VATTR_INIT(&va);VATTR_SET(&va,va_type,VDIR);VATTR_SET(&va,va_mode,0755);
    VATTR_SET(&va,va_uid,0);VATTR_SET(&va,va_gid,0);
    error=vn_mkdir(nd.ni_dvp,&vp,&nd,&va,ctx);
    if(!error){
        vnode_update_identity(vp,nd.ni_dvp,nd.ni_cnd.cn_nameptr,nd.ni_cnd.cn_namelen,nd.ni_cnd.cn_hash,VNODE_UPDATE_NAME|VNODE_UPDATE_PARENT);
        error=VNOP_FSYNC(vp,MNT_WAIT,ctx);if(!error)error=VNOP_FSYNC(nd.ni_dvp,MNT_WAIT,ctx);
    }
    if(vp)vnode_put(vp);
out:lookup_done(&nd);return error;
}
static int b2l_apps_sync_payload(vnode_t vp,const unsigned char *data,size_t bytes,
    unsigned mode,bool writing,vfs_context_t ctx)
{
    if(!bytes||bytes>4*1024*1024||!root_type(vp,VREG))return EINVAL;
    struct vnode_attr va;VATTR_INIT(&va);
    VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);VATTR_WANTED(&va,va_nlink);VATTR_WANTED(&va,va_data_size);
    int error=vnode_getattr(vp,&va,ctx);if(error)return error;
    if(!VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_mode)||!VATTR_IS_SUPPORTED(&va,va_nlink)||!VATTR_IS_SUPPORTED(&va,va_data_size)||
       va.va_uid||(va.va_mode&0777)!=mode||va.va_nlink<1||va.va_nlink>(writing?1:2)||va.va_data_size<0||
       (uint64_t)va.va_data_size>bytes||(!writing&&(uint64_t)va.va_data_size!=bytes))return EINVAL;
    unsigned char check[256];size_t size=(size_t)va.va_data_size;
    for(size_t offset=0;offset<size;){
        size_t n=size-offset;if(n>sizeof(check))n=sizeof(check);int resid=0;
        error=vn_rdwr(UIO_READ,vp,(caddr_t)check,(int)n,(off_t)offset,UIO_SYSSPACE,0,kauth_cred_get(),&resid,current_proc());
        if(error||resid||!b2l_apps_payload_equal(data,bytes,offset,check,n,
            !writing&&mode==0755&&data==b2l_apps_executable))return error?error:EIO;
        offset+=n;
    }
    if(!writing)return 0;
    for(size_t offset=size;offset<bytes;){
        size_t n=bytes-offset;if(n>4096)n=4096;int resid=0;
        error=vn_rdwr(UIO_WRITE,vp,(caddr_t)data+offset,(int)n,(off_t)offset,UIO_SYSSPACE,IO_SYNC,kauth_cred_get(),&resid,current_proc());
        if(error||resid)return error?error:EIO;offset+=n;
    }
    error=VNOP_FSYNC(vp,MNT_WAIT,ctx);if(error)return error;
    return b2l_apps_sync_payload(vp,data,bytes,mode,false,ctx);
}
static int b2l_apps_publish(const char *final,const char *staging,const unsigned char *data,size_t bytes,unsigned mode,vfs_context_t ctx)
{
    vnode_t vp=NULL;int error=vnode_lookup(final,VNODE_LOOKUP_NOFOLLOW_ANY,&vp,ctx);
    if(!error){error=b2l_apps_sync_payload(vp,data,bytes,mode,false,ctx);vnode_put(vp);return error;}
    if(error!=ENOENT)return error;
    error=vnode_open(staging,FREAD|FWRITE|O_CREAT,mode,VNODE_LOOKUP_NOFOLLOW_ANY,&vp,ctx);
    if(error)return error;
    error=b2l_apps_sync_payload(vp,data,bytes,mode,true,ctx);
    if(!error){
        struct nameidata nd;NDINIT(&nd,CREATE,OP_LINK,LOCKPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(final),ctx);nd.ni_flag|=NAMEI_NOFOLLOW_ANY;
        error=namei(&nd);
        if(!error){
            if(nd.ni_vp)error=EEXIST;
            else if(!root_type(nd.ni_dvp,VDIR))error=EXDEV;
            else {error=VNOP_LINK(vp,nd.ni_dvp,&nd.ni_cnd,ctx);if(!error)error=VNOP_FSYNC(nd.ni_dvp,MNT_WAIT,ctx);}
            lookup_done(&nd);
        }
    }
    int close_error=vnode_close(vp,FREAD|FWRITE,ctx);return error?error:close_error;
}
#include "b2l_profile_cache.h"
static void b2l_userspace_before_init(void)
{
    int requested=0;PE_parse_boot_argn("b2l_userspace_bootstrap",&requested,sizeof(requested));
    if(requested!=1||!b2l_amfi_active()||!rootvnode||vfs_isrdonly(vnode_mount(rootvnode))||strcmp(vfs_statfs(vnode_mount(rootvnode))->f_fstypename,"hfs"))return;
    vfs_context_t ctx=vfs_context_current();int error=b2l_profile_cache_refresh(ctx);
    printf("B2L PROFILE: MobileGestalt refresh error=%d\n",error);
    error=0;
    static const char *const dirs[]={"/usr/local","/usr/local/libexec","/System/Library/LaunchDaemons","/private/var/db"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++)if((error=b2l_apps_directory(dirs[i],ctx)))goto out;
    error=b2l_apps_publish("/usr/local/libexec/b2l-system-app-bootstrap-v2","/usr/local/libexec/.b2l-system-app-bootstrap-v2",b2l_apps_executable,sizeof(b2l_apps_executable),0755,ctx);
    if(error)goto out;
    error=b2l_apps_publish("/System/Library/LaunchDaemons/dev.b2l.system-app-bootstrap-v2.plist","/System/Library/LaunchDaemons/.b2l-system-app-bootstrap-v2.plist",b2l_apps_job,sizeof(b2l_apps_job),0644,ctx);
    if(error)goto out;
    error=b2l_apps_publish("/private/var/db/b2l-launchd-bootstrap-v3.plist","/private/var/db/.b2l-launchd-bootstrap-v3",b2l_apps_launch_cache,sizeof(b2l_apps_launch_cache),0644,ctx);
    if(!error)error=VFS_SYNC(vnode_mount(rootvnode),MNT_WAIT,ctx);
    if(!error)b2l_apps_cache_ready=1;
out:printf("B2L APPS: native migration bootstrap publication error=%d\n",error);
}
