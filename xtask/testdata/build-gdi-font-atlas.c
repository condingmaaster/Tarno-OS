// SPDX-License-Identifier: GPL-2.0-or-later
// Bake the existing THOS Lato faces with host FreeType. Runtime GDI uses only
// immutable coverage bytes and integer blending, with no kernel FPU or TTF parser.
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void u32(FILE*f,uint32_t x){unsigned char b[4]={x,x>>8,x>>16,x>>24};if(fwrite(b,1,4,f)!=4)exit(2);}
int main(int argc,char**argv){
 if(argc!=4)return 2;
 FT_Library lib;if(FT_Init_FreeType(&lib))return 3;
 const unsigned sizes[]={8,10,12,14,16,18,20,22,24,26,28,32,36,40,48,56,64};
 unsigned cps[200],n=0;for(unsigned c=32;c<=126;c++)cps[n++]=c;for(unsigned c=160;c<=255;c++)cps[n++]=c;
 const unsigned extra[]={0x2013,0x2014,0x2018,0x2019,0x201c,0x201d,0x2022,0x2026,0x20ac};for(unsigned i=0;i<9;i++)cps[n++]=extra[i];if(n!=200)return 4;
 unsigned ns=sizeof sizes/sizeof sizes[0],nf=ns*2;long data_start=16+nf*32+nf*n*28;FILE*out=fopen(argv[3],"wb+");if(!out)return 5;fseek(out,data_start-1,SEEK_SET);fputc(0,out);fseek(out,0,SEEK_SET);fwrite("THFA",1,4,out);u32(out,1);u32(out,nf);u32(out,n);
 for(unsigned face=0;face<2;face++){
  FT_Face ft;if(FT_New_Face(lib,argv[1+face],0,&ft))return 6;
  for(unsigned s=0;s<ns;s++){
   unsigned font=face*ns+s;unsigned records=16+nf*32+font*n*28;if(FT_Set_Pixel_Sizes(ft,0,sizes[s]))return 7;
   FT_Size_Metrics m=ft->size->metrics;TT_OS2*os2=FT_Get_Sfnt_Table(ft,ft_sfnt_os2);
   int avg=os2?((int)os2->xAvgCharWidth*(int)sizes[s]+ft->units_per_EM/2)/ft->units_per_EM:((m.max_advance+32)>>6);
   fseek(out,16+font*32,SEEK_SET);u32(out,sizes[s]);u32(out,face?700:400);u32(out,(m.ascender+63)>>6);u32(out,(-m.descender+63)>>6);u32(out,(m.height+63)>>6);u32(out,(m.max_advance+63)>>6);u32(out,records);u32(out,avg);
   for(unsigned c=0;c<n;c++){
    if(!FT_Get_Char_Index(ft,cps[c])||FT_Load_Char(ft,cps[c],FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL))return 8;
    FT_GlyphSlot g=ft->glyph;FT_Bitmap bm=g->bitmap;if(bm.width&&bm.rows&&bm.pixel_mode!=FT_PIXEL_MODE_GRAY)return 9;
    fseek(out,0,SEEK_END);long offset=ftell(out);if(offset<0||offset>0xffffffffL)return 10;
    for(unsigned row=0;row<bm.rows;row++){const unsigned char*p=bm.buffer+(bm.pitch>=0?row*(unsigned)bm.pitch:(bm.rows-1-row)*(unsigned)-bm.pitch);if(fwrite(p,1,bm.width,out)!=bm.width)return 11;}
    fseek(out,records+c*28,SEEK_SET);u32(out,cps[c]);u32(out,(g->advance.x+32)>>6);u32(out,g->bitmap_left);u32(out,g->bitmap_top);u32(out,bm.width);u32(out,bm.rows);u32(out,(uint32_t)offset);
   }
  }
  FT_Done_Face(ft);
 }
 int major,minor,patch;FT_Library_Version(lib,&major,&minor,&patch);fseek(out,0,SEEK_END);printf("Lato atlas: %u fonts, %u glyphs/font, %ld bytes; FreeType %d.%d.%d\n",nf,n,ftell(out),major,minor,patch);if(fclose(out))return 12;FT_Done_FreeType(lib);return 0;
}
