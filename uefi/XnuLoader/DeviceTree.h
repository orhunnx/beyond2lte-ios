/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_DEVICE_TREE_H
#define B2L_DEVICE_TREE_H
#include "BootArgs.h"
/* rel/xnu-12377 prng_random requires this full bootloader seed length. */
#define B2L_SEED_BYTES 256U
struct b2l_dt_config {
    b2l_u64 memory_base, memory_size, mpidr;
    b2l_u32 counter_frequency;
    unsigned char random_seed[B2L_SEED_BYTES];
    struct b2l_boot_video video;
};
/* Apple flattened device tree: native little endian, property names 32 bytes,
 * lengths and node counts 32 bits, property contents padded to four bytes.
 * Includes the mandatory /chosen/memory-map and /defaults containers without
 * fabricating trust-cache, auxiliary-kernel or protected I/O memory ranges. */
int b2l_build_device_tree(void *buffer, b2l_size capacity,
                         const struct b2l_dt_config *config, b2l_size *used);
#endif
