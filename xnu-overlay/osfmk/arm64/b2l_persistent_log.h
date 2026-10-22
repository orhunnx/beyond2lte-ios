/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_PERSISTENT_LOG_H
#define B2L_PERSISTENT_LOG_H
#include "b2l_log_core.h"
/* Append is safe before VM/scheduler initialization. It does not access disk. */
void b2l_log_append(char c);
/* Call once after native UFS is ready, outside its mutex. No formatting occurs. */
int b2l_log_start(const struct b2l_log_sink *sink);
#endif
