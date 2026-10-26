/* SPDX-License-Identifier: BSD-2-Clause
 * Select the verified derived launch cache for the native cache loader only.
 * Existing cache files, service entries and signature policy stay intact.
 */
#ifndef B2L_EXEC_BOOTSTRAP_CACHE_H
#define B2L_EXEC_BOOTSTRAP_CACHE_H
extern int b2l_apps_cache_is_ready(void);
static int b2l_exec_bootstrap_cache(struct image_params *imgp,int pointer_size)
{
    int enabled=0;
    if(pointer_size!=8||imgp->ip_argc!=1||
       !PE_parse_boot_argn("b2l_userspace_bootstrap",&enabled,sizeof(enabled))||enabled!=1||
       !b2l_apps_cache_is_ready())return 0;
    const char *path=imgp->ip_strings+strlen(EXECUTABLE_KEY);
    if(strcmp(path,"/usr/libexec/launchd_cache_loader"))return 0;
    static const char cache[]="/private/var/db/b2l-launchd-bootstrap-v3.plist";
    static const char signature[]="/System/Library/xpc/launchd.plist.sig";
    size_t bytes=sizeof(cache)+sizeof(signature);
    if(imgp->ip_argspace<(int)(bytes+5*pointer_size)||imgp->ip_strspace<(int)bytes)return E2BIG;
    int error=exec_add_user_string(imgp,CAST_USER_ADDR_T(cache),UIO_SYSSPACE,TRUE);if(error)return error;
    error=exec_add_user_string(imgp,CAST_USER_ADDR_T(signature),UIO_SYSSPACE,TRUE);if(error)return error;
    imgp->ip_argspace-=2*pointer_size;imgp->ip_argc+=2;
    b2l_early_trace("B2L APPS: native launch cache loader selected verified bootstrap cache\n");
    return 0;
}
#endif
