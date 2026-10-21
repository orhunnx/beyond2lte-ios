/* SPDX-License-Identifier: BSD-2-Clause
 * Real MD5 for legacy HFS volume UUIDs, using the same pinned MIT BearSSL input
 * as AES. This does not register a partial global crypto function table.
 */
#if defined(ARM64_BOARD_CONFIG_B2L)
#include <libkern/libkern.h>
#include "b2l_md5.h"
#include "b2l_bearssl/src/hash/md5.c"
#include "b2l_bearssl/src/codec/dec32le.c"
#include "b2l_bearssl/src/codec/enc32le.c"

_Static_assert(sizeof(MD5_CTX) == 88, "legacy MD5 context ABI");
_Static_assert(offsetof(MD5_CTX, count) == 16, "legacy MD5 count ABI");
_Static_assert(offsetof(MD5_CTX, buffer) == 24, "legacy MD5 buffer offset ABI");
_Static_assert(sizeof(((MD5_CTX *)0)->buffer) == 64, "legacy MD5 buffer ABI");
_Static_assert(sizeof(((MD5_CTX *)0)->state) == 16, "legacy MD5 state ABI");

static void
b2l_md5_to_legacy(const br_md5_context *input, MD5_CTX *output)
{
	/* Preserve XNU's existing adapter: count is bytes, high word first. */
	output->count[0] = (uint32_t)(input->count >> 32);
	output->count[1] = (uint32_t)input->count;
	memcpy(output->state, input->val, sizeof(output->state));
	memcpy(output->buffer, input->buf, sizeof(output->buffer));
}

static void
b2l_md5_from_legacy(const MD5_CTX *input, br_md5_context *output)
{
	br_md5_init(output);
	output->count = ((uint64_t)input->count[0] << 32) | input->count[1];
	memcpy(output->val, input->state, sizeof(input->state));
	memcpy(output->buf, input->buffer, sizeof(input->buffer));
}

void
b2l_md5_init(MD5_CTX *ctx)
{
	br_md5_context state;
	memset(&state, 0, sizeof(state));
	br_md5_init(&state);
	b2l_md5_to_legacy(&state, ctx);
}

void
b2l_md5_update(MD5_CTX *ctx, const void *data, unsigned int size)
{
	br_md5_context state;
	b2l_md5_from_legacy(ctx, &state);
	br_md5_update(&state, data, size);
	b2l_md5_to_legacy(&state, ctx);
}

void
b2l_md5_final(unsigned char digest[MD5_DIGEST_LENGTH], MD5_CTX *ctx)
{
	br_md5_context state;
	b2l_md5_from_legacy(ctx, &state);
	br_md5_out(&state, digest);
}
#endif
