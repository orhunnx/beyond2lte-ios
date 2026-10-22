/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BLIT_H
#define B2L_BLIT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* The source and destination extents are real mapped allocations. Never derive
 * memory bounds solely from userspace width/height. No resize or YUV fallback. */
static inline int b2l_blit(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||sx<0||sy<0||dx<0||dy<0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh||
       width<=0||height<=0)return 0;
    uint32_t w=(uint32_t)width,h=(uint32_t)height;
    if(w>sw-(uint32_t)sx)w=sw-(uint32_t)sx;
    if(w>dw-(uint32_t)dx)w=dw-(uint32_t)dx;
    if(h>sh-(uint32_t)sy)h=sh-(uint32_t)sy;
    if(h>dh-(uint32_t)dy)h=dh-(uint32_t)dy;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+h-1)*ss+((uint64_t)sx+w)*4;
    uint64_t end_d=((uint64_t)dy+h-1)*ds+((uint64_t)dx+w)*4;
    if(end_s>sbytes||end_d>dbytes)return 0;
    /* memmove tolerates the default scanout surface being re-presented. */
    for(uint32_t y=0;y<h;++y)
        memmove((uint8_t*)dst+((uint64_t)dy+y)*ds+(uint64_t)dx*4,
                (const uint8_t*)src+soff+((uint64_t)sy+y)*ss+(uint64_t)sx*4,(size_t)w*4);
    return 1;
}
#endif
