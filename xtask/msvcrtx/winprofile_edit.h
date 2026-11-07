/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pure bounded UTF16 INI editing foundation. No file IO/API export here.
 * Preserve unrelated lines/comments, edit the first matching section/key.
 * ASCII case-insensitive identifiers; other UTF16 units compare exactly.
 * Registry IniFileMapping, full Unicode folding and duplicate-section merging
 * are outside this explicit profile. */
#ifndef THOS_WINPROFILE_EDIT_H
#define THOS_WINPROFILE_EDIT_H
#include <stddef.h>
#include <stdint.h>
#define PI_MAX_UNITS 131072U
struct pi_line {size_t begin,end,next,name,names,value,values;unsigned kind;};
static int pi_space(uint16_t c){return c==' '||c=='\t';}
static unsigned pi_fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int pi_equal(const uint16_t*a,size_t an,const uint16_t*b,size_t bn){if(an!=bn)return 0;for(size_t n=0;n<an;n++)if(pi_fold(a[n])!=pi_fold(b[n]))return 0;return 1;}
static struct pi_line pi_line(const uint16_t*text,size_t units,size_t at){
 struct pi_line l={0};l.begin=at;while(at<units&&text[at]!='\r'&&text[at]!='\n')at++;l.end=at;
 if(at<units){uint16_t c=text[at++];if(c=='\r'&&at<units&&text[at]=='\n')at++;}l.next=at;
 size_t a=l.begin,b=l.end;while(a<b&&pi_space(text[a]))a++;while(b>a&&pi_space(text[b-1]))b--;
 if(a==b||text[a]==';')return l;
 if(text[a]=='['){size_t end=a+1;while(end<b&&text[end]!=']')end++;if(end==b)return l;l.kind=1;l.name=a+1;l.names=end-a-1;return l;}
 size_t eq=a;while(eq<b&&text[eq]!='=')eq++;if(eq==b)return l;
 size_t k=eq;while(k>a&&pi_space(text[k-1]))k--;if(k==a)return l;l.kind=2;l.name=a;l.names=k-a;
 l.value=eq+1;while(l.value<b&&pi_space(text[l.value]))l.value++;l.values=b-l.value;
 return l;
}
struct pi_plan {size_t begin,end;int heading,write;};
static struct pi_plan pi_plan(const uint16_t*t,size_t n,const uint16_t*section,size_t sn,const uint16_t*key,size_t kn,int value){
 size_t heading=n,body=n,end=n;int found=0;
 for(size_t at=0;at<n;){struct pi_line l=pi_line(t,n,at);at=l.next;if(l.kind==1){if(found){end=l.begin;break;}if(pi_equal(t+l.name,l.names,section,sn)){found=1;heading=l.begin;body=l.next;}}}
 if(!key)return found?(struct pi_plan){heading,end,0,0}:(struct pi_plan){n,n,0,0};
 if(found){for(size_t at=body;at<end;){struct pi_line l=pi_line(t,n,at);at=l.next;if(l.kind==2&&pi_equal(t+l.name,l.names,key,kn))return (struct pi_plan){l.begin,l.next,0,value};}
  return (struct pi_plan){end,end,0,value};}
 return (struct pi_plan){n,n,value,value};
}
static unsigned pi_name(const uint16_t*s,size_t n,int key){if(!s||!n||n>1024)return 87;for(size_t j=0;j<n;j++)if(!s[j]||s[j]=='\r'||s[j]=='\n'||(key?s[j]=='=':s[j]=='['||s[j]==']'))return 87;return 0;}
struct pi_output{uint16_t*p;size_t count;};
static void pi_put(struct pi_output*out,uint16_t c){if(out->p)out->p[out->count]=c;out->count++;}
static void pi_copy(struct pi_output*out,const uint16_t*t,size_t n){for(size_t j=0;j<n;j++)pi_put(out,t[j]);}
static void pi_render(struct pi_output*out,const uint16_t*t,size_t n,struct pi_plan plan,const uint16_t*s,size_t sn,const uint16_t*k,size_t kn,const uint16_t*v,size_t vn){
 pi_copy(out,t,plan.begin);
 if(plan.write){if(plan.begin&&t[plan.begin-1]!='\r'&&t[plan.begin-1]!='\n'){pi_put(out,'\r');pi_put(out,'\n');}
  if(plan.heading){pi_put(out,'[');pi_copy(out,s,sn);pi_put(out,']');pi_put(out,'\r');pi_put(out,'\n');}
  pi_copy(out,k,kn);pi_put(out,'=');pi_copy(out,v,vn);pi_put(out,'\r');pi_put(out,'\n');}
 if(n>plan.end)pi_copy(out,t+plan.end,n-plan.end);
}
static unsigned pi_edit(const uint16_t*t,size_t n,const uint16_t*s,size_t sn,const uint16_t*k,size_t kn,const uint16_t*v,size_t vn,uint16_t*out,size_t cap,size_t*needed){
 if(n>PI_MAX_UNITS||(!t&&n))return 223;unsigned e=pi_name(s,sn,0);if(e)return e;if(k&&(e=pi_name(k,kn,1)))return e;
 if(vn>32766)return 87;for(size_t j=0;j<vn;j++)if(!v||!v[j]||v[j]=='\r'||v[j]=='\n')return 87;
 for(size_t j=0;j<n;j++)if(!t[j])return 13;
 struct pi_plan plan=pi_plan(t,n,s,sn,k,kn,v!=0);struct pi_output measure={0};pi_render(&measure,t,n,plan,s,sn,k,kn,v,vn);*needed=measure.count;
 if(measure.count>PI_MAX_UNITS)return 223;if(!out)return 0;if(cap<measure.count)return 122;
 struct pi_output result={out,0};pi_render(&result,t,n,plan,s,sn,k,kn,v,vn);return 0;
}
#endif
