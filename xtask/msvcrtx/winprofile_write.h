/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary on-disk INI profile only: no IniFileMapping registry/cache layer.
 * Existing UTF16LE BOM stays Unicode; new/ANSI files stay ACP1252. Unmappable
 * ANSI output fails1113 before replacing the original, rather than losing data.
 * A stable per-directory sidecar flock serializes THOS profile writers across
 * threads/processes, including atomic replacement. External raw writers must
 * cooperate. No daemon or retained file-content cache.
 */
#include "winprofile_edit.h"
static unsigned profile_errno(i64 r){switch(-r){case 2:return 3;case 13:case 1:case 21:case 40:return 5;case 12:return 8;case 28:return 112;case 22:return 87;case 36:return 206;case 4:return 995;case 95:return 50;default:return 1117;}}
static unsigned profile_len(const u16*s,size_t max,size_t*out){if(!s){*out=0;return 0;}for(*out=0;*out<=max;(*out)++)if(!s[*out])return 0;return 87;}
static unsigned profile_path(const u16*name,char*out,size_t cap,size_t*directory){
 size_t n;unsigned e=profile_len(name,1024,&n);if(e||!name||!n)return 87;size_t at=0,start=0;
 if(n>=2&&name[1]==':'){if((name[0]!='C'&&name[0]!='c')||n<3||(name[2]!='/'&&name[2]!='\\'))return 50;start=2;}
 else if(name[0]!='/'&&name[0]!='\\'){const char*prefix="/Windows/";for(size_t j=0;prefix[j];j++)out[at++]=prefix[j];}
 if(n>=2&&(name[0]=='/'||name[0]=='\\')&&(name[1]=='/'||name[1]=='\\'))return 50;
 for(size_t j=start;j<n;j++){u32 c=name[j];if(c==':'||!c)return 87;if(c=='\\')c='/';if(c>=0xd800&&c<=0xdbff){if(j+1==n||name[j+1]<0xdc00||name[j+1]>0xdfff)return 1113;c=0x10000+((c-0xd800)<<10)+(name[++j]-0xdc00);}else if(c>=0xdc00&&c<=0xdfff)return 1113;
  unsigned char b[4];unsigned count;if(c<0x80){b[0]=c;count=1;}else if(c<0x800){b[0]=0xc0|(c>>6);b[1]=0x80|(c&63);count=2;}else if(c<0x10000){b[0]=0xe0|(c>>12);b[1]=0x80|((c>>6)&63);b[2]=0x80|(c&63);count=3;}else{b[0]=0xf0|(c>>18);b[1]=0x80|((c>>12)&63);b[2]=0x80|((c>>6)&63);b[3]=0x80|(c&63);count=4;}
  if(count>=cap-at)return 206;for(unsigned k=0;k<count;k++)out[at++]=b[k];}
 out[at]=0;if(!at||out[at-1]=='/')return 87;size_t component=1;*directory=0;
 for(size_t j=1;j<=at;j++)if(j==at||out[j]=='/'){size_t len=j-component;if(!len||(len==1&&out[component]=='.')||(len==2&&out[component]=='.'&&out[component+1]=='.'))return 87;if(j<at)*directory=j+1;component=j+1;}
 if(!*directory)*directory=1;return 0;
}
static unsigned profile_ansi(u16 c,unsigned char*out){if(c<128||(c>=160&&c<=255)){*out=(unsigned char)c;return 0;}for(unsigned n=0;n<32;n++)if(env_cp1252[n]==c){*out=(unsigned char)(n+128);return 0;}return 1113;}
/* The current backend does not enforce O_NOFOLLOW. Explicitly reject
 * observed final symlinks. This is not a general adversarial filesystem
 * race guarantee; noncooperating raw namespace mutation remains unsupported. */
static unsigned profile_no_symlink(const char*path){unsigned char st[144];i64 r=sc6(S_NEWFSTATAT,-100,(i64)path,(i64)st,0x100,0,0);if(r==-2)return 0;if(r<0)return profile_errno(r);u32 type=wr_u32(st+24)&0xf000U;return type==0xa000U?50:type!=0x8000U?5:0;}
static u32 profile_counter;
EXPORT BOOL WINAPI WritePrivateProfileStringW(const u16*section,const u16*key,const u16*value,const u16*filename){
 /* This implementation has no cached profile or registry mapping to flush. */
 if(!section&&!key&&!value){SetLastError(0);return 0;}
 size_t sn=0,kn=0,vn=0;unsigned e=profile_len(section,1024,&sn);if(!e)e=pi_name(section,sn,0);if(!e&&key){e=profile_len(key,1024,&kn);if(!e)e=pi_name(key,kn,1);}if(!e)e=profile_len(value,32766,&vn);
 if(e){SetLastError(e);return 0;}
 char path[4096],lockpath[4096],temp[4096];size_t dir=0;e=profile_path(filename,path,sizeof path,&dir);if(e){SetLastError(e);return 0;}
 const char*lockname=".thos-profile.lock";size_t ln=slen(lockname);if(dir+ln>=sizeof lockpath){SetLastError(206);return 0;}for(size_t j=0;j<dir;j++)lockpath[j]=path[j];for(size_t j=0;j<=ln;j++)lockpath[dir+j]=lockname[j];
 const size_t stride=PI_MAX_UNITS*2+8,allocation=stride*4;i64 memory=sc6(S_MMAP,0,allocation,3,34,-1,0);if(memory<0){SetLastError(profile_errno(memory));return 0;}
 unsigned char*raw=(unsigned char*)memory,*encoded=raw+stride*3;u16*input=(u16*)(raw+stride),*output=(u16*)(raw+stride*2);
 i64 lockfd=-1,fd=-1,tempfd=-1;int temporary=0;size_t bytes=0,units=0,needed=0;int unicode=0;u32 mode=0600;BOOL result=0;
 e=profile_no_symlink(lockpath);if(e)goto done;
 lockfd=sc3(S_OPEN,lockpath,2|64|0x20000|0x80000|0x800,0600);if(lockfd<0){e=profile_errno(lockfd);goto done;}
 unsigned char statbuf[144];i64 r=sc3(5,lockfd,statbuf,0);if(r<0){e=profile_errno(r);goto done;}if((wr_u32(statbuf+24)&0xf000U)!=0x8000U){e=5;goto done;}
 r=sc3(91,lockfd,0600,0);if(r<0){e=profile_errno(r);goto done;}
 r=sc3(73,lockfd,2,0);if(r<0){e=profile_errno(r);goto done;}
 e=profile_no_symlink(path);if(e)goto done;
 fd=sc3(S_OPEN,path,2|0x20000|0x80000|0x800,0);if(fd<0&&fd!=-2){e=profile_errno(fd);goto done;}
 if(fd>=0){r=sc3(5,fd,statbuf,0);if(r<0){e=profile_errno(r);goto done;}u32 bits=wr_u32(statbuf+24);if((bits&0xf000U)!=0x8000U){e=5;goto done;}mode=bits&0777;
  u64 size=(u64)wr_u32(statbuf+48)|((u64)wr_u32(statbuf+52)<<32);if(size>PI_MAX_UNITS*2+2){e=223;goto done;}
  while(bytes<stride){r=sc3(S_READ,fd,raw+bytes,stride-bytes);if(r<0){e=profile_errno(r);goto done;}if(!r)break;bytes+=(size_t)r;}if(bytes>PI_MAX_UNITS*2+2){e=223;goto done;}
  r=sc3(S_CLOSE,fd,0,0);fd=-1;if(r<0){e=profile_errno(r);goto done;}}
 if(bytes>=2&&raw[0]==0xff&&raw[1]==0xfe){unicode=1;if(bytes&1){e=13;goto done;}units=(bytes-2)/2;for(size_t j=0;j<units;j++)input[j]=(u16)raw[2+2*j]|((u16)raw[3+2*j]<<8);}
 else {if((bytes>=2&&raw[0]==0xfe&&raw[1]==0xff)||(bytes>=3&&raw[0]==0xef&&raw[1]==0xbb&&raw[2]==0xbf)){e=50;goto done;}if(bytes>PI_MAX_UNITS){e=223;goto done;}units=bytes;for(size_t j=0;j<units;j++)input[j]=raw[j]>=128&&raw[j]<160?env_cp1252[raw[j]-128]:raw[j];}
 e=pi_edit(input,units,section,sn,key,kn,value,vn,output,PI_MAX_UNITS,&needed);if(e)goto done;
 if(needed==units){size_t j=0;for(;j<units&&input[j]==output[j];j++);if(j==units){result=1;goto done;}}
 size_t encoded_bytes=0;if(unicode){encoded[encoded_bytes++]=0xff;encoded[encoded_bytes++]=0xfe;for(size_t j=0;j<needed;j++){encoded[encoded_bytes++]=(unsigned char)output[j];encoded[encoded_bytes++]=(unsigned char)(output[j]>>8);}}
 else {for(size_t j=0;j<needed;j++){e=profile_ansi(output[j],encoded+encoded_bytes);if(e)goto done;encoded_bytes++;}}
 if(dir+50>=sizeof temp){e=206;goto done;}for(size_t j=0;j<dir;j++)temp[j]=path[j];const char*prefix=".thos-profile-";size_t at=dir;for(size_t j=0;prefix[j];j++)temp[at++]=prefix[j];u32 pid=(u32)sc3(S_GETPID,0,0,0),counter=__atomic_add_fetch(&profile_counter,1,__ATOMIC_RELAXED);for(int j=7;j>=0;j--)temp[at++]="0123456789abcdef"[(pid>>(4*j))&15];temp[at++]='-';for(int j=7;j>=0;j--)temp[at++]="0123456789abcdef"[(counter>>(4*j))&15];temp[at++]='.';temp[at++]='t';temp[at++]='m';temp[at++]='p';temp[at]=0;
 tempfd=sc3(S_OPEN,temp,1|64|128|0x20000|0x80000,mode);if(tempfd<0){e=profile_errno(tempfd);goto done;}temporary=1;
 r=sc3(91,tempfd,mode,0);if(r<0){e=profile_errno(r);goto done;}
 for(size_t written=0;written<encoded_bytes;){r=sc3(S_WRITE,tempfd,encoded+written,encoded_bytes-written);if(r<=0){e=r<0?profile_errno(r):1117;goto done;}written+=(size_t)r;}
 r=sc3(S_FSYNC,tempfd,0,0);if(r<0){e=profile_errno(r);goto done;}r=sc3(S_CLOSE,tempfd,0,0);tempfd=-1;if(r<0){e=profile_errno(r);goto done;}
 r=sc3(S_RENAME,temp,path,0);if(r<0){e=profile_errno(r);goto done;}temporary=0;result=1;
 done:
 if(tempfd>=0)sc3(S_CLOSE,tempfd,0,0);if(temporary)sc3(S_UNLINK,temp,0,0);if(fd>=0)sc3(S_CLOSE,fd,0,0);if(lockfd>=0)sc3(S_CLOSE,lockfd,0,0);sc3(S_MUNMAP,memory,allocation,0);if(!result)SetLastError(e);return result;
}
