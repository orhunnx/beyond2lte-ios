/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * B2L legacy AES CBC/ECB provider backed by BearSSL's portable bitslice AES.
 * Upstream sources and MIT license are staged by scripts/stage-bearssl.py.
 * No heap, SIMD or CPU crypto extension is needed. Contexts can be copied:
 * their only pointer is the immutable BearSSL class descriptor.
 */
#if defined(ARM64_BOARD_CONFIG_B2L)
#include <libkern/libkern.h>
#include <libkern/crypto/aes.h>
#include <corecrypto/ccmode.h>
#include <corecrypto/cc_error.h>
#include "b2l_bearssl/inc/bearssl_block.h"

_Static_assert(sizeof(br_aes_ct_cbcenc_keys) <= sizeof(aes_encrypt_ctx),
    "BearSSL encryption context does not fit the XNU AES ABI");
_Static_assert(sizeof(br_aes_ct_cbcdec_keys) <= sizeof(aes_decrypt_ctx),
    "BearSSL decryption context does not fit the XNU AES ABI");

static int
b2l_aes_encrypt_init(const struct ccmode_cbc *mode, cccbc_ctx *ctx,
    size_t key_len, const void *key)
{
    (void)mode;
    if (ctx == NULL) {
        return CCERR_PARAMETER;
    }
    cc_clear(sizeof(br_aes_ct_cbcenc_keys), ctx);
    if (key == NULL || (key_len != 16 && key_len != 24 && key_len != 32)) {
        return CCERR_PARAMETER;
    }
    br_aes_ct_cbcenc_init((br_aes_ct_cbcenc_keys *)(void *)ctx, key, key_len);
    return CCERR_OK;
}

static int
b2l_aes_decrypt_init(const struct ccmode_cbc *mode, cccbc_ctx *ctx,
    size_t key_len, const void *key)
{
    (void)mode;
    if (ctx == NULL) {
        return CCERR_PARAMETER;
    }
    cc_clear(sizeof(br_aes_ct_cbcdec_keys), ctx);
    if (key == NULL || (key_len != 16 && key_len != 24 && key_len != 32)) {
        return CCERR_PARAMETER;
    }
    br_aes_ct_cbcdec_init((br_aes_ct_cbcdec_keys *)(void *)ctx, key, key_len);
    return CCERR_OK;
}

static int
b2l_aes_check(const void *ctx, const void *iv, size_t nblocks,
    const void *in, void *out, unsigned rounds)
{
    if (ctx == NULL || iv == NULL ||
        (rounds != 10 && rounds != 12 && rounds != 14)) {
        return CCERR_PARAMETER;
    }
    if (nblocks > SIZE_MAX / 16) {
        return CCERR_OVERFLOW;
    }
    if (nblocks != 0 && (in == NULL || out == NULL)) {
        return CCERR_PARAMETER;
    }
    return CCERR_OK;
}

static int
b2l_aes_encrypt_cbc(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks,
    const void *in, void *out)
{
    const br_aes_ct_cbcenc_keys *key = (const void *)ctx;
    int error = b2l_aes_check(ctx, iv, nblocks, in, out,
        key == NULL ? 0 : key->num_rounds);
    if (error != CCERR_OK || nblocks == 0) {
        return error;
    }
    /* BearSSL is in-place; copy first so both corecrypto API forms work. */
    memmove(out, in, nblocks * 16);
    br_aes_ct_cbcenc_run(key, iv, out, nblocks * 16);
    return CCERR_OK;
}

static int
b2l_aes_decrypt_cbc(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks,
    const void *in, void *out)
{
    const br_aes_ct_cbcdec_keys *key = (const void *)ctx;
    int error = b2l_aes_check(ctx, iv, nblocks, in, out,
        key == NULL ? 0 : key->num_rounds);
    if (error != CCERR_OK || nblocks == 0) {
        return error;
    }
    memmove(out, in, nblocks * 16);
    br_aes_ct_cbcdec_run(key, iv, out, nblocks * 16);
    return CCERR_OK;
}

const struct ccmode_cbc b2l_aes_cbc_encrypt = {
    .size = sizeof(br_aes_ct_cbcenc_keys),
    .block_size = 16,
    .init = b2l_aes_encrypt_init,
    .cbc = b2l_aes_encrypt_cbc,
};
const struct ccmode_cbc b2l_aes_cbc_decrypt = {
    .size = sizeof(br_aes_ct_cbcdec_keys),
    .block_size = 16,
    .init = b2l_aes_decrypt_init,
    .cbc = b2l_aes_decrypt_cbc,
};
#endif /* ARM64_BOARD_CONFIG_B2L */
