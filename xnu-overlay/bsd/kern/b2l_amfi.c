/* SPDX-License-Identifier: BSD-2-Clause
 * Metadata-only AMFI adapter for the explicitly unsigned B2L development boot.
 * No signature-validity, page-hash, trust-cache or identity claim is made.
 */
#ifndef B2L_AMFI_HOST_TEST
#include <kern/startup.h>
#include <pexpert/pexpert.h>
#include <libkern/amfi/amfi.h>
#include <libkern/libkern.h>
#include <sys/codesign.h>
#include <sys/kauth.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/proc_internal.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <security/mac_framework.h>
#include <security/mac_policy.h>
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#endif
#include "b2l_amfi.h"
#include "b2l_amfi_metadata.h"
static int b2l_amfi_slot=-1;
static bool b2l_amfi_registered;
static bool b2l_amfi_requested(void)
{return b2l_arm64e_compat_enabled()&&!cs_process_global_enforcement();}
bool b2l_amfi_active(void)
{return b2l_amfi_registered&&b2l_amfi_requested();}
/* The rootfs development platform policy is independent of XML availability. */
bool b2l_amfi_vnode_is_platform(struct vnode *vp)
{
    mount_t mp;
    return b2l_amfi_active()&&vp&&vnode_vtype(vp)==VREG&&
           (mp=vnode_mount(vp))&&(vfs_flags(mp)&MNT_ROOTFS);
}
static void *label_ent(struct label *label)
{return label&&b2l_amfi_slot>=0?(void*)mac_label_get(label,b2l_amfi_slot):NULL;}
static void label_replace(struct label *label,void*owned)
{
    if(!label||b2l_amfi_slot<0){b2l_ent_release(owned);return;}
    void*old=label_ent(label);mac_label_set(label,b2l_amfi_slot,(intptr_t)owned);
    b2l_ent_release(old);
}
static void label_init(struct label *label)
{if(b2l_amfi_slot>=0)mac_label_set(label,b2l_amfi_slot,0);}
static void label_destroy(struct label *label){label_replace(label,NULL);}
static void label_associate(kauth_cred_t parent,kauth_cred_t child)
{
    void*e=label_ent(mac_cred_label(parent));b2l_ent_retain(e);
    label_replace(mac_cred_label(child),e);
}
/* Query owns a credential reference for the entire object retain operation.
 * Credential labels are immutable after publication; no PID map or text-vnode
 * pointer is retained. Returned OSObject reference outlives credential swaps. */
static void *proc_copy_ent(proc_t process)
{
    if(!process||!b2l_amfi_active()||b2l_amfi_slot<0)return NULL;
    kauth_cred_t cred=kauth_cred_proc_ref(process);
    if(!cred)return NULL;
    void*e=label_ent(mac_cred_label(cred));b2l_ent_retain(e);
    kauth_cred_unref(&cred);return e;
}
bool b2l_amfi_is_platform(proc_t process)
{
    /* Explicit CS_OPS_CLEARPLATFORM clears this native flag. The immutable
     * label must not reassert classification after that request. */
    if(!process||!(proc_getcsflags(process)&CS_PLATFORM_BINARY))return false;
    void*e=proc_copy_ent(process);bool result=b2l_ent_platform(e);
    b2l_ent_release(e);return result;
}
bool b2l_amfi_cache_entitled(proc_t process)
{
    if (!b2l_amfi_active() || !b2l_amfi_is_platform(process)) return false;
    void *e = proc_copy_ent(process);
    bool allowed = b2l_ent_bool(e, "com.apple.private.amfi.can-check-trust-cache");
    b2l_ent_release(e);
    return allowed;
}
int b2l_amfi_proc_copy_cdhash(proc_t process,uint8_t out[20],uint8_t*type)
{
 if(out)memset(out,0,20);if(type)*type=0;if(!out||!type)return EINVAL;
 void*e=proc_copy_ent(process);int error=b2l_ent_copy_cdhash(e,out,type);
 b2l_ent_release(e);return error;
}
int b2l_amfi_proc_copy_xml(proc_t process,void**out,size_t*length)
{
    if(out)*out=NULL;if(length)*length=0;
    if(!out||!length)return EINVAL;
    void*e=proc_copy_ent(process);bool ok=b2l_ent_copy_xml(e,out,length);
    b2l_ent_release(e);return ok?0:ENOENT;
}
int b2l_amfi_proc_copy_der(proc_t process,void**out,size_t*length)
{
    if(out)*out=NULL;if(length)*length=0;
    if(!out||!length)return EINVAL;
    void*e=proc_copy_ent(process);bool ok=b2l_ent_copy_der(e,out,length);
    b2l_ent_release(e);return ok?0:ENOENT;
}
int b2l_amfi_proc_copy_identity(proc_t process,void**out,size_t*length)
{
    if(out)*out=NULL;if(length)*length=0;
    if(!out||!length)return EINVAL;
    void*e=proc_copy_ent(process);int error=b2l_ent_copy_identity(e,out,length);
    b2l_ent_release(e);return error;
}
int b2l_amfi_proc_copy_dict(proc_t process,void **out)
{
    if(!out)return EINVAL;*out=NULL;
    void*e=proc_copy_ent(process);*out=b2l_ent_copy_dict(e);
    b2l_ent_release(e);return 0;
}
struct vnode_reader {struct vnode*vp;};
static int vnode_read_metadata(void*opaque,uint64_t at,void*out,size_t length)
{
    struct vnode_reader*r=opaque;int resid=0;
    if(at>INT64_MAX||length>INT_MAX)return EINVAL;
    int error=vn_rdwr(UIO_READ,r->vp,(caddr_t)out,(int)length,(off_t)at,
                      UIO_SYSSPACE,0,kauth_cred_get(),&resid,current_proc());
    return error?error:(resid?EIO:0);
}
static void *vnode_copy_ent(struct vnode*vp,off_t offset)
{
    if(!b2l_amfi_vnode_is_platform(vp)||offset<0)return NULL;
    off_t bytes=0;
    if(vnode_size(vp,&bytes,vfs_context_current())||bytes<0)return b2l_ent_create(NULL,0,true);
    void*xml=kalloc_data(B2L_AMFI_XML_MAX,Z_WAITOK);
    if(!xml)return NULL;
    struct vnode_reader input={vp};
    struct b2l_amfi_reader reader={(uint64_t)bytes,vnode_read_metadata,&input};
    size_t length=0;
    enum b2l_amfi_metadata_result result=b2l_amfi_read_xml(&reader,(uint64_t)offset,xml,B2L_AMFI_XML_MAX,&length);
    char identifier[B2L_AMFI_IDENTITY_MAX];size_t identifier_len=0;
    enum b2l_amfi_metadata_result id_result=b2l_amfi_read_identity(&reader,(uint64_t)offset,
        identifier,sizeof(identifier),&identifier_len);
    const void*id=id_result==B2L_AMFI_XML_PRESENT?identifier:NULL;
    if(!id)identifier_len=0;
    void*e=b2l_ent_create_metadata(result==B2L_AMFI_XML_PRESENT?xml:NULL,
        result==B2L_AMFI_XML_PRESENT?length:0,true,id,identifier_len);
    /* Malformed XML must not discard a separately well-formed identifier. */
    if(!e&&id)e=b2l_ent_create_metadata(NULL,0,true,id,identifier_len);
    /* Read the original DER slot while this credential object is unpublished.
     * The opaque bytes are copied, never converted into a fabricated CE context. */
    if(!e)e=b2l_ent_create(NULL,0,true);
    size_t der_length=0;
    if(e&&b2l_amfi_read_der(&reader,(uint64_t)offset,xml,B2L_AMFI_XML_MAX,&der_length)==B2L_AMFI_XML_PRESENT&&
       !b2l_ent_attach_der(e,xml,der_length)){
        b2l_ent_release(e);e=NULL;
    }
    uint8_t hash[20],hash_type=0;
    if(e&&b2l_amfi_read_cdhash(&reader,(uint64_t)offset,hash,&hash_type)==B2L_AMFI_XML_PRESENT)
        (void)b2l_ent_attach_cdhash(e,hash,hash_type);
    kfree_data(xml,B2L_AMFI_XML_MAX);
    /* Malformed/absent/DER-only XML grants no entitlements. Rootfs platform
     * classification is the separate, explicitly enabled development policy. */
    return e?e:b2l_ent_create(NULL,0,true);
}
int b2l_amfi_vnode_copy_dict(struct vnode *vp,off_t offset,void **out)
{
    if(!out||!vp||offset<0)return EINVAL;*out=NULL;
    void*e=vnode_copy_ent(vp,offset);*out=b2l_ent_copy_dict(e);
    b2l_ent_release(e);return 0;
}
bool b2l_amfi_vnode_has_cdhash(struct vnode *vp,off_t offset)
{
    if(!vp||offset<0||!b2l_amfi_vnode_is_platform(vp))return false;
    void*e=vnode_copy_ent(vp,offset);uint8_t hash[20],type=0;
    bool present=e&&b2l_ent_copy_cdhash(e,hash,&type)==0;
    b2l_ent_release(e);return present;
}
static int label_will_exec(kauth_cred_t old __unused,struct vnode*vp __unused,
    off_t offset __unused,struct vnode*script __unused,struct label*vl __unused,
    struct label*sl __unused,struct label*el __unused,struct proc*p __unused,
    void*attr __unused,size_t length __unused)
{return b2l_amfi_active()?1:0;}
static int label_exec(kauth_cred_t old __unused,kauth_cred_t new,
    struct proc*p __unused,struct vnode*vp,off_t offset,struct vnode*script __unused,
    struct label*vl __unused,struct label*sl __unused,struct label*el __unused,
    unsigned*csflags,void*attr __unused,size_t length __unused,int*disjoint __unused)
{
    /* Clear inherited metadata for every executable, including non-rootfs
     * executables and scripts. Do not trust metadata from the script itself. */
    bool platform=b2l_amfi_vnode_is_platform(vp);
    void*e=vnode_copy_ent(vp,offset);
    if(e){
        void*identity=NULL;size_t identity_len=0;
        if(!b2l_ent_copy_identity(e,&identity,&identity_len)){
            const char *id=identity;
            if(strnstr(id,"containermanager",identity_len)||strnstr(id,"backboard",identity_len)||strnstr(id,"lsd",identity_len)||strnstr(id,"ManagedSettings",identity_len)||strnstr(id,"appprotection",identity_len)){
                static unsigned samples;
                if(__atomic_fetch_add(&samples,1,__ATOMIC_RELAXED)<32){
                    uint8_t hash[20],type=0;
                    b2l_early_trace("B2L EXECMETA: id=%s system_container=%d manager=%d cdhash_error=%d\n",
                        id,b2l_ent_bool(e,"com.apple.security.system-container"),
                        b2l_ent_bool(e,"com.apple.private.security.container-manager"),b2l_ent_copy_cdhash(e,hash,&type));
                }
            }
            /* Original metadata only: TCC runtime results are observed in
             * b2l_diag.c, with no additional authorization or entitlement. */
            int requested=0;PE_parse_boot_argn("b2l_diag",&requested,sizeof(requested));
            if(requested==1&&(!strcmp(id,"com.apple.assetsd")||!strcmp(id,"com.apple.tccd"))){
                static unsigned tcc_exec_samples;
                if(__atomic_fetch_add(&tcc_exec_samples,1,__ATOMIC_RELAXED)<16){
                    void *der=NULL;size_t der_len=0;uint8_t hash[20],type=0;
                    bool has_der=b2l_ent_copy_der(e,&der,&der_len);
                    b2l_early_trace("B2L TCCMETA: id=%s photos_allow=%d der=%d der_bytes=%llu cdhash_error=%d\n",
                        id,b2l_ent_string(e,"com.apple.private.tcc.allow","kTCCServicePhotos"),
                        has_der,(unsigned long long)der_len,b2l_ent_copy_cdhash(e,hash,&type));
                    if(der)kfree_data(der,der_len);
                }
            }
            kfree_data(identity,identity_len);
        }
    }
    label_replace(mac_cred_label(new),e);
    if(platform&&!e){
        if(csflags)*csflags&=~CS_PLATFORM_BINARY;
        return ENOMEM; /* Exec cannot publish an inconsistent platform task. */
    }
    if(b2l_amfi_active()&&csflags){
        *csflags&=~CS_PLATFORM_BINARY;
        if(b2l_ent_platform(e))*csflags|=CS_PLATFORM_BINARY;
    }
    return 0;
}
static kern_return_t ent_bool(const void*e,const char*n)
{return b2l_ent_bool(e,n)?KERN_SUCCESS:KERN_DENIED;}
static kern_return_t ent_string(const void*e,const char*n,const char*v)
{return b2l_ent_string(e,n,v)?KERN_SUCCESS:KERN_DENIED;}
static kern_return_t ent_copy(const void*e,const char*n,void**out)
{
    if(out)*out=NULL;if(!out)return KERN_INVALID_ARGUMENT;
    *out=b2l_ent_copy_object(e,n);return *out?KERN_SUCCESS:KERN_NOT_FOUND;
}
static kern_return_t proc_bool(const proc_t p,const char*n)
{void*e=proc_copy_ent(p);kern_return_t r=ent_bool(e,n);b2l_ent_release(e);return r;}
static kern_return_t proc_string(const proc_t p,const char*n,const char*v)
{void*e=proc_copy_ent(p);kern_return_t r=ent_string(e,n,v);b2l_ent_release(e);return r;}
static kern_return_t proc_object(const proc_t p,const char*n,void**out)
{void*e=proc_copy_ent(p);kern_return_t r=ent_copy(e,n,out);b2l_ent_release(e);return r;}
static void ent_invalidate(void*e __unused){}
static void *ent_dict(void*e){return b2l_ent_copy_dict(e);}
static bool ent_xml(void*e,CS_GenericBlob**out)
{size_t length;return b2l_ent_copy_xml(e,(void**)out,&length);}
static bool ent_der(void*e __unused,const CS_GenericBlob**out)
{if(out)*out=NULL;return false;}
static bool unavailable_bytes(const uint8_t**out,size_t*length)
{if(out)*out=NULL;if(length)*length=0;return false;}
static void *unavailable_context(CEQueryContext_t context __unused){return NULL;}
static kern_return_t no_adjust(void*e __unused,struct cs_blob*b __unused)
{return KERN_NOT_SUPPORTED;}
static kern_return_t no_adjust_context(void*e __unused,struct cs_blob*b __unused,const CEContext_t*c __unused)
{return KERN_NOT_SUPPORTED;}
static kern_return_t no_adjust_monitor(void*e __unused,const CEQueryContext_t c __unused,
    const void*s __unused,const char*i __unused,const uint32_t f __unused)
{return KERN_NOT_SUPPORTED;}
static const amfi_t b2l_amfi={
 .OSEntitlements_invalidate=ent_invalidate,.OSEntitlements_asdict=ent_dict,
 .OSEntitlements_get_transmuted=ent_der,.OSEntitlements_get_xml=ent_xml,
 .get_legacy_profile_exemptions=unavailable_bytes,.get_udid=unavailable_bytes,
 .query_context_to_object=unavailable_context,
 /* CoreEntitlements opaque-query and TrustCache interfaces stay unavailable.
  * No opaque CE context/error object is fabricated. */
 .OSEntitlements={.version=OSENTITLEMENTS_INTERFACE_VERSION,
  .adjustContext=no_adjust_context,.adjustContextWithMonitor=no_adjust_monitor,
  .adjustContextWithoutMonitor=no_adjust,
  .queryEntitlementBoolean=ent_bool,.queryEntitlementBooleanWithProc=proc_bool,
  .queryEntitlementString=ent_string,.queryEntitlementStringWithProc=proc_string,
  .copyEntitlementAsOSObject=ent_copy,.copyEntitlementAsOSObjectWithProc=proc_object}
};
static void register_interface(void)
{
    if(!b2l_amfi_requested()||amfi)return;
    amfi_interface_register(&b2l_amfi);b2l_amfi_registered=true;
    b2l_early_trace("B2L AMFI: unsigned XML metadata interface registered; signatures unverified\n");
}
STARTUP(EARLY_BOOT,STARTUP_RANK_MIDDLE,register_interface);
#include "b2l_amfi_constraint.h"
static const struct mac_policy_ops b2l_amfi_ops={
 .mpo_cred_label_init=label_init,.mpo_cred_label_destroy=label_destroy,
 .mpo_cred_label_associate=label_associate,
 .mpo_cred_check_label_update_execve=label_will_exec,
 .mpo_cred_label_update_execve=label_exec
};
static struct mac_policy_conf b2l_amfi_conf={
 .mpc_name="B2LDevMetadata",.mpc_fullname="B2L unsigned rootfs XML metadata policy",
 .mpc_ops=&b2l_amfi_ops,.mpc_field_off=&b2l_amfi_slot
};
static mac_policy_handle_t b2l_amfi_handle;
void b2l_amfi_policy_register(void)
{
    if(!b2l_amfi_active())return;
    int error=mac_policy_register(&b2l_amfi_conf,&b2l_amfi_handle,NULL);
    if(error){b2l_amfi_slot=-1;b2l_amfi_registered=false;}
    else b2l_amfi_constraint_register();
    printf("B2L AMFI: development credential metadata policy %s (%d)\n",error?"unavailable":"registered",error);
}
