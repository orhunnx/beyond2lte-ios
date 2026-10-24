/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_AMFI_H
#define B2L_AMFI_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
struct proc;
struct vnode;
#ifdef __cplusplus
extern "C" {
#endif
bool b2l_amfi_active(void);
bool b2l_amfi_is_platform(struct proc *);
bool b2l_amfi_cache_entitled(struct proc *);
bool b2l_amfi_vnode_is_platform(struct vnode *);
int b2l_amfi_proc_copy_dict(struct proc *, void **);
int b2l_amfi_vnode_copy_dict(struct vnode *, int64_t, void **);
bool b2l_amfi_vnode_has_cdhash(struct vnode *, int64_t);
int b2l_amfi_proc_copy_cdhash(struct proc *, uint8_t[20], uint8_t*);
bool b2l_ent_attach_cdhash(void *, const uint8_t[20], uint8_t);
int b2l_ent_copy_cdhash(const void *, uint8_t[20], uint8_t*);
int b2l_amfi_proc_copy_der(struct proc *, void **, size_t *);
int b2l_amfi_proc_copy_xml(struct proc *, void **, size_t *);
int b2l_amfi_proc_copy_identity(struct proc *, void **, size_t *);
void b2l_amfi_policy_register(void);
void *b2l_ent_create(const void *, size_t, bool);
void *b2l_ent_create_metadata(const void *, size_t, bool, const void *, size_t);
int b2l_ent_copy_identity(const void *, void **, size_t *);
bool b2l_ent_attach_der(void *, const void *, size_t);
bool b2l_ent_copy_der(const void *, void **, size_t *);
void b2l_ent_retain(void *);
void b2l_ent_release(void *);
bool b2l_ent_platform(const void *);
bool b2l_ent_bool(const void *, const char *);
bool b2l_ent_string(const void *, const char *, const char *);
void *b2l_ent_copy_object(const void *, const char *);
void *b2l_ent_copy_dict(const void *);
bool b2l_ent_copy_xml(const void *, void **, size_t *);
#ifdef __cplusplus
}
#endif
#endif
