#pragma once
#include <linux/types.h>
void udelay(unsigned long us); void mdelay(unsigned long ms); void usleep_range(unsigned long a, unsigned long b);
#include <linux/skbuff.h>
#include <linux/bcma/bcma.h>
#include <linux/bitops.h>
#define pr_debug(...) do{}while(0)
#define pr_err(...) do{}while(0)
#define printk(...) 0
#define KERN_ERR ""
#define ARRAY_SIZE_(a) 0
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define roundup(x,y) ((((x)+((y)-1))/(y))*(y))
#define swap(a,b) do{__typeof__(a) _t=(a);(a)=(b);(b)=_t;}while(0)
#define clamp(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define __stringify(x) #x
#define offsetof_ offsetof
#include <bcma_ids.h>
void *kzalloc(size_t, int); void *kmalloc(size_t, int); void *kmalloc_array(size_t, size_t, int); void *kcalloc(size_t, size_t, int); void kfree(const void*);
void bcma_maskset16(struct bcma_device*, u16, u16, u16); void bcma_maskset32(struct bcma_device*, u16, u32, u32);
u32 int_sqrt(unsigned long);
#define WARN(c, ...) (c)
#define EBADE 52
#define __printf(a,b)
#define DIV_ROUND_CLOSEST(x,d) (((x)+((d)/2))/(d))
static inline int hweight8(unsigned v){int c=0;while(v&0xff){c+=v&1;v>>=1;}return c;}
static inline int fls(unsigned x){int r=0;while(x){r++;x>>=1;}return r;}
