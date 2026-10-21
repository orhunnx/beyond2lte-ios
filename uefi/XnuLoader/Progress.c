/* SPDX-License-Identifier: BSD-2-Clause */
#include "Progress.h"
/* Only the capitals needed by the six fixed labels; 5x7, enlarged eightfold. */
static const b2l_u8 *Glyph(char Ch)
{
    static const b2l_u8 A[]={14,17,17,31,17,17,17}, B[]={30,17,17,30,17,17,30};
    static const b2l_u8 C[]={14,17,16,16,16,17,14}, D[]={30,17,17,17,17,17,30};
    static const b2l_u8 E[]={31,16,16,30,16,16,31}, F[]={31,16,16,30,16,16,16};
    static const b2l_u8 H[]={17,17,17,31,17,17,17}, I[]={31,4,4,4,4,4,31};
    static const b2l_u8 J[]={7,2,2,2,18,18,12}, K[]={17,18,20,24,20,18,17};
    static const b2l_u8 L[]={16,16,16,16,16,16,31}, M[]={17,27,21,21,17,17,17};
    static const b2l_u8 N[]={17,25,21,19,17,17,17}, O[]={14,17,17,17,17,17,14};
    static const b2l_u8 P[]={30,17,17,30,16,16,16}, R[]={30,17,17,30,20,18,17};
    static const b2l_u8 S[]={15,16,16,14,1,1,30}, T[]={31,4,4,4,4,4,4};
    static const b2l_u8 U[]={17,17,17,17,17,17,14}, V[]={17,17,17,17,17,10,4};
    static const b2l_u8 W[]={17,17,17,21,21,21,10}, X[]={17,17,10,4,10,17,17};
    static const b2l_u8 Y[]={17,17,10,4,4,4,4};
    static const b2l_u8 Z[]={0,0,0,0,0,0,0};
    switch(Ch){case'A':return A;case'B':return B;case'C':return C;case'D':return D;
    case'E':return E;case'F':return F;case'H':return H;case'I':return I;
    case'J':return J;case'K':return K;case'L':return L;case'M':return M;
    case'N':return N;case'O':return O;case'P':return P;case'R':return R;
    case'S':return S;case'T':return T;case'U':return U;case'V':return V;
    case'W':return W;case'X':return X;case'Y':return Y;default:return Z;}
}
static void Rect(b2l_u64 Base, b2l_u64 Stride, b2l_size X, b2l_size Y, b2l_size W, b2l_size H, b2l_u32 Color)
{
    for(b2l_size Row=0;Row<H;++Row){volatile b2l_u32 *P=(void *)(b2l_size)(Base+(Y+Row)*Stride+X*4);
      for(b2l_size Col=0;Col<W;++Col) P[Col]=Color;}
    #ifdef B2L_UEFI_BUILD
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}
void B2lProgressSet(const struct b2l_handoff_info *I, b2l_size Stage, b2l_u32 Color)
{ if(I->panel_base && Stage<6) Rect(I->panel_base,I->stride,0,Stage*96,128,64,Color); }
void B2lProgressPrepare(b2l_u64 Base, b2l_u64 Stride, b2l_u64 Width, b2l_u64 Height,
                        struct b2l_handoff_info *I)
{
    static const char *Labels[]={"MAP READY","EBS RETURN","HANDOFF","CACHE CLEAN","MMU OFF","KERNEL JUMP"};
    b2l_size X0, Y0;
    I->panel_base=0;
    if(Width<1184 || Height<864) return;
    X0=(b2l_size)((Width-1184)/2); Y0=(b2l_size)(Height/3);
    I->panel_base=Base+Y0*Stride+X0*4; I->stride=Stride;
    Rect(I->panel_base,Stride,0,0,1184,6*96,0);
    for(b2l_size S=0;S<6;++S){
      B2lProgressSet(I,S,0x00303030);
      for(b2l_size C=0;Labels[S][C];++C){const b2l_u8 *G=Glyph(Labels[S][C]);
        for(b2l_size Y=0;Y<7;++Y)for(b2l_size X=0;X<5;++X)if(G[Y]&(1U<<(4-X)))
          Rect(I->panel_base,Stride,192+C*48+X*8,S*96+Y*8,8,8,0x00ffffff);
      }
    }
}
