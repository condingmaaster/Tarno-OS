// SPDX-License-Identifier: GPL-2.0-or-later
// Test fixtures request a CLIENT size. Real CreateWindowEx takes the OUTER size.
// Keep pixel assertions and the existing visible THOS window sizes unchanged.
static HWND thos_test_client_window(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,
 int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID param){
 RECT r={0,0,width,height};
 if(!AdjustWindowRectEx(&r,style,menu!=0,ex))return 0;
 return CreateWindowExA(ex,cls,title,style,x,y,r.right-r.left,r.bottom-r.top,parent,menu,instance,param);
}
