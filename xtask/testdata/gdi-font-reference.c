// SPDX-License-Identifier: GPL-2.0-or-later
// Independent test oracle: draw from the original TrueType faces using host
// FreeType, without reading the baked atlas or the kernel text implementation.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
#define W 640
#define H 360
static unsigned char pixels[W*H*3];
static void draw(FT_Face face,int size,int x,int y,const wchar_t*text,unsigned r,unsigned g,unsigned b){
 if(FT_Set_Pixel_Sizes(face,0,size))return;int baseline=y+((face->size->metrics.ascender+63)>>6);unsigned colors[3]={r,g,b};
 for(;*text;text++){
  if(FT_Load_Char(face,(FT_ULong)*text,FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL))return;
  FT_GlyphSlot glyph=face->glyph;FT_Bitmap*bm=&glyph->bitmap;
  for(unsigned row=0;row<bm->rows;row++)for(unsigned col=0;col<bm->width;col++){
   int dx=x+glyph->bitmap_left+(int)col,dy=baseline-glyph->bitmap_top+(int)row;if(dx<0||dy<0||dx>=W||dy>=H)continue;
   unsigned off=bm->pitch>=0?row*(unsigned)bm->pitch:(bm->rows-1-row)*(unsigned)-bm->pitch;unsigned a=bm->buffer[off+col];
   for(unsigned c=0;c<3;c++){unsigned char*p=&pixels[(dy*W+dx)*3+c];*p=(colors[c]*a+*p*(255-a)+127)/255;}
  }
  x+=(glyph->advance.x+32)>>6;
 }
}
int main(int argc,char**argv){if(argc!=4)return 2;FT_Library library;FT_Face regular,bold;if(FT_Init_FreeType(&library)||FT_New_Face(library,argv[1],0,&regular)||FT_New_Face(library,argv[2],0,&bold))return 3;
 for(unsigned i=0;i<W*H;i++){pixels[i*3]=14;pixels[i*3+1]=17;pixels[i*3+2]=24;}
 draw(bold,32,24,32,L"THOS Windows",235,240,248);draw(regular,16,24,100,L"Schriften: ä ö ü ß €",156,172,204);draw(regular,26,24,142,L"Text mit echter Kantenglättung",100,220,180);
 FILE*out=fopen(argv[3],"wb");if(!out)return 4;fprintf(out,"P6\n%d %d\n255\n",W,H);if(fwrite(pixels,1,sizeof pixels,out)!=sizeof pixels)return 5;return fclose(out)?6:0;
}
