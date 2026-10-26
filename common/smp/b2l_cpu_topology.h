/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_CPU_TOPOLOGY_H
#define B2L_CPU_TOPOLOGY_H
/* Logical scheduling groups; independent of firmware and raw MPIDR IDs. */
static inline unsigned b2l_cpu_cluster(unsigned cpu) { return cpu < 4 ? 0 : cpu < 6 ? 1 : cpu < 8 ? 2 : 3; }
static inline unsigned b2l_cpu_cluster_core(unsigned cpu) { return cpu < 4 ? cpu : cpu < 6 ? cpu-4 : cpu-6; }
static inline unsigned b2l_cpu_cluster_type(unsigned cpu) { return cpu < 4 ? 'E' : cpu < 6 ? 'M' : 'P'; }
#endif
