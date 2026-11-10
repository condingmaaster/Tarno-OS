/* SPDX-License-Identifier: GPL-2.0-or-later
 * Small supported metadata catalogue, not complete installed Windows NLS.
 * Formatting/collation/calendar APIs and unknown fields remain unsupported.
 * Both catalogue records use ACP1252; system UI/default locale is de-DE.
 * Include after winenv.h for the existing ACP encoder.
 */
struct nls_item {u32 field;const u16 *de,*en;u32 number_de,number_en;int numeric;};
#define NLS_TEXT(k,de,en) {k,L##de,L##en,0,0,0}
#define NLS_NUMBER(k,de,en,d,e) {k,L##de,L##en,d,e,1}
static const struct nls_item nls_items[]={
 NLS_NUMBER(0x1,"0407","0409",0x407,0x409),
 NLS_TEXT(0x2,"Deutsch (Deutschland)","Englisch (Vereinigte Staaten)"),
 NLS_TEXT(0x3,"DEU","ENU"),NLS_TEXT(0x4,"Deutsch","English"),
 NLS_NUMBER(0x5,"49","1",49,1),NLS_TEXT(0x6,"Deutschland","Vereinigte Staaten"),
 NLS_TEXT(0x7,"DEU","USA"),NLS_TEXT(0x8,"Deutschland","United States"),
 NLS_NUMBER(0x9,"0407","0409",0x407,0x409),NLS_NUMBER(0xa,"49","1",49,1),
 NLS_NUMBER(0xb,"850","437",850,437),NLS_TEXT(0xc,";",","),NLS_NUMBER(0xd,"0","1",0,1),
 NLS_TEXT(0xe,",","."),NLS_TEXT(0xf,".",","),NLS_TEXT(0x10,"3;0","3;0"),
 NLS_NUMBER(0x11,"2","2",2,2),NLS_NUMBER(0x12,"1","1",1,1),NLS_TEXT(0x13,"0123456789","0123456789"),
 NLS_TEXT(0x14,"\u20ac","$"),NLS_TEXT(0x15,"EUR","USD"),NLS_TEXT(0x16,",","."),NLS_TEXT(0x17,".",","),NLS_TEXT(0x18,"3;0","3;0"),
 NLS_NUMBER(0x19,"2","2",2,2),NLS_NUMBER(0x1a,"2","2",2,2),NLS_NUMBER(0x1b,"3","0",3,0),NLS_NUMBER(0x1c,"8","0",8,0),
 NLS_TEXT(0x1d,".","/"),NLS_TEXT(0x1e,":",":"),NLS_TEXT(0x1f,"dd.MM.yyyy","M/d/yyyy"),NLS_TEXT(0x20,"dddd, d. MMMM yyyy","dddd, MMMM d, yyyy"),
 NLS_NUMBER(0x21,"1","0",1,0),NLS_NUMBER(0x22,"1","0",1,0),NLS_NUMBER(0x23,"1","0",1,0),NLS_NUMBER(0x24,"1","1",1,1),NLS_NUMBER(0x25,"1","0",1,0),NLS_NUMBER(0x26,"1","0",1,0),NLS_NUMBER(0x27,"1","0",1,0),
 NLS_TEXT(0x28,"","AM"),NLS_TEXT(0x29,"","PM"),NLS_TEXT(0x50,"+","+"),NLS_TEXT(0x51,"-","-"),
 NLS_TEXT(0x59,"de","en"),NLS_TEXT(0x5a,"DE","US"),NLS_TEXT(0x5c,"de-DE","en-US"),NLS_NUMBER(0x70,"0","0",0,0),
 NLS_TEXT(0x72,"German (Germany)","English (United States)"),NLS_TEXT(0x73,"Deutsch (Deutschland)","English (United States)"),
 NLS_TEXT(0x1001,"German","English"),NLS_TEXT(0x1002,"Germany","United States"),NLS_TEXT(0x1003,"HH:mm:ss","h:mm:ss tt"),NLS_NUMBER(0x1004,"1252","1252",1252,1252),
 NLS_NUMBER(0x100c,"0","6",0,6),NLS_NUMBER(0x100d,"2","0",2,0),NLS_NUMBER(0x1010,"1","1",1,1)
};
#undef NLS_TEXT
#undef NLS_NUMBER
static int nls_info(u32 lcid,u32 flags,void *buf,int n,int wide){
 if(n<0){SetLastError(87);return 0;}
 if(lcid==0||lcid==0x400||lcid==0x800)lcid=0x407;
 if(lcid!=0x407&&lcid!=0x409){SetLastError(87);return 0;}
 if(flags&0x1fff0000U){SetLastError(1004);return 0;}
 u32 field=flags&0xffff;const struct nls_item *item=0;
 for(unsigned i=0;i<sizeof nls_items/sizeof nls_items[0];i++)if(nls_items[i].field==field){item=&nls_items[i];break;}
 if(!item){SetLastError(1004);return 0;}
 if(flags&0x20000000U){
  if(!item->numeric){SetLastError(1004);return 0;}
  int need=wide?2:4;if(n==0)return need;if(!buf||n<need){SetLastError(122);return 0;}
  u32 value=lcid==0x407?item->number_de:item->number_en;unsigned char *b=buf;for(unsigned i=0;i<4;i++)b[i]=(unsigned char)(value>>(8*i));return need;
 }
 const u16 *s=lcid==0x407?item->de:item->en;int need=1;while(s[need-1])need++;
 if(n==0)return need;if(!buf||n<need){SetLastError(122);return 0;}
 if(wide){u16 *d=buf;for(int i=0;i<need;i++)d[i]=s[i];}else{unsigned char *d=buf;for(int i=0;i<need;i++)d[i]=env_ansi(s[i]);}
 return need;
}
EXPORT int WINAPI GetLocaleInfoA(u32 lcid,u32 type,char *buf,int n){return nls_info(lcid,type,buf,n,0);}
EXPORT int WINAPI GetLocaleInfoW(u32 lcid,u32 type,u16 *buf,int n){return nls_info(lcid,type,buf,n,1);}
typedef BOOL(WINAPI *nls_enum_a)(char*);
typedef BOOL(WINAPI *nls_enum_w)(u16*);
static int nls_enum_options(const void *cb,u32 flags){if(!cb){SetLastError(87);return 0;}if((flags&~7U)||(flags&3U)==3U){SetLastError(1004);return 0;}return 1;}
static const char nls_ids[][9]={"00000407","00000409"};
EXPORT BOOL WINAPI EnumSystemLocalesA(nls_enum_a cb,u32 flags){if(!nls_enum_options((const void*)cb,flags))return 0;if(flags==4||(flags&1U))return 1;for(unsigned i=0;i<2;i++){char id[9];for(unsigned j=0;j<9;j++)id[j]=nls_ids[i][j];if(!cb(id))break;}return 1;}
EXPORT BOOL WINAPI EnumSystemLocalesW(nls_enum_w cb,u32 flags){if(!nls_enum_options((const void*)cb,flags))return 0;if(flags==4||(flags&1U))return 1;for(unsigned i=0;i<2;i++){u16 id[9];for(unsigned j=0;j<9;j++)id[j]=(unsigned char)nls_ids[i][j];if(!cb(id))break;}return 1;}

/* INSTALLED means full translation/keyboard/font/sorting support on Windows.
 * This metadata-only catalogue must not claim that complete capability. */
EXPORT BOOL WINAPI IsValidLocale(u32 lcid,u32 flags){
 if(flags!=1&&flags!=2){SetLastError(1004);return 0;}
 if(lcid==0||lcid==0x400||lcid==0x800)lcid=0x407;
 if(lcid!=0x407&&lcid!=0x409)return 0;
 if(flags==1){SetLastError(50);return 0;}return 1;
}
