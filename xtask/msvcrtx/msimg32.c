// SPDX-License-Identifier: GPL-2.0-or-later
// The public MSIMG32 entry delegates to the real GDI32 blending backend.
#include "wincommon.h"
typedef struct { unsigned char op, flags, constant_alpha, alpha_format; } BlendFunction;
__declspec(dllimport) BOOL WINAPI GdiAlphaBlend(HANDLE,int,int,int,int,HANDLE,int,int,int,int,BlendFunction);
EXPORT BOOL WINAPI AlphaBlend(HANDLE d,int x,int y,int w,int h,HANDLE s,int sx,int sy,int sw,int sh,BlendFunction blend){
    return GdiAlphaBlend(d,x,y,w,h,s,sx,sy,sw,sh,blend);
}
EXPORT BOOL WINAPI DllMain(HANDLE h,unsigned reason,void *reserved){(void)h;(void)reason;(void)reserved;return 1;}
