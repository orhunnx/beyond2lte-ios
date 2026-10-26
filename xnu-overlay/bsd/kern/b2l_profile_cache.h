/* SPDX-License-Identifier: BSD-2-Clause
 * One-time profile transition before launchd starts. Native MobileGestalt
 * recreates its cache; an exclusive rename keeps the complete old inode.
 */
static int b2l_profile_cache_refresh(vfs_context_t ctx)
{
    static const char oldpath[]="/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist";
    static const char backup[]="/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.b2l-before-profile-v2.plist";
    vnode_t saved=NULL;int error=vnode_lookup(backup,VNODE_LOOKUP_NOFOLLOW_ANY,&saved,ctx);
    if(!error){int valid=root_type(saved,VREG);vnode_put(saved);return valid?0:EINVAL;}
    if(error!=ENOENT)return error;
    struct nameidata from,to;bool have_to=false;
    NDINIT(&from,DELETE,OP_UNLINK,WANTPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(oldpath),ctx);
    from.ni_flag|=NAMEI_NOFOLLOW_ANY;
    error=namei(&from);if(error)return error==ENOENT?0:error;
    if(!root_type(from.ni_vp,VREG)||!root_type(from.ni_dvp,VDIR)){error=EINVAL;goto done;}
    struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_nlink);VATTR_WANTED(&va,va_data_size);
    error=vnode_getattr(from.ni_vp,&va,ctx);if(error)goto done;
    if(!VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_nlink)||!VATTR_IS_SUPPORTED(&va,va_data_size)||(va.va_uid!=0&&va.va_uid!=501)||va.va_nlink!=1||va.va_data_size<=0||va.va_data_size>4*1024*1024){error=EINVAL;goto done;}
    NDINIT(&to,RENAME,OP_RENAME,WANTPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(backup),ctx);
    to.ni_flag|=NAMEI_NOFOLLOW_ANY;
    error=namei(&to);if(error)goto done;have_to=true;
    if(to.ni_vp){error=EEXIST;goto done;}
    if(to.ni_dvp!=from.ni_dvp){error=EXDEV;goto done;}
    error=VNOP_FSYNC(from.ni_vp,MNT_WAIT,ctx);if(error)goto done;
    error=vn_rename(from.ni_dvp,&from.ni_vp,&from.ni_cnd,NULL,to.ni_dvp,&to.ni_vp,&to.ni_cnd,NULL,VFS_RENAME_EXCL,ctx);
    if(!error)error=VNOP_FSYNC(from.ni_dvp,MNT_WAIT,ctx);
    if(!error)printf("B2L PROFILE: previous MobileGestalt cache backed up; native regeneration pending\n");
done:if(have_to)lookup_done(&to);lookup_done(&from);return error;
}
