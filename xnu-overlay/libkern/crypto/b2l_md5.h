/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_MD5_H
#define B2L_MD5_H
#include <libkern/crypto/md5.h>
void b2l_md5_init(MD5_CTX *);
void b2l_md5_update(MD5_CTX *, const void *, unsigned int);
void b2l_md5_final(unsigned char [MD5_DIGEST_LENGTH], MD5_CTX *);
#endif
