#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u16 __le16; typedef u32 __le32; typedef u64 __le64; typedef u16 __be16; typedef u32 __be32;
typedef unsigned int uint; typedef unsigned long ulong;
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __iomem
#define __user
#define __force
#define __init
#define __exit
#define __must_check
#define __maybe_unused __attribute__((unused))
#define __always_unused __attribute__((unused))
#define likely(x) __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
#define BIT(n) (1UL<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define abs(x) ((x)<0?-(x):(x))
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define container_of(ptr,type,member) ((type*)((char*)(ptr)-offsetof(type,member)))
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define WARN_ON(x) (x)
#define WARN_ON_ONCE(x) (x)
#define BUG_ON(x) ((void)(x))
#define ETH_ALEN 6
#define ENOMEM 12
#define EINVAL 22
#define ENODEV 19
#define EIO 5
#define EOPNOTSUPP 95
#define GFP_ATOMIC 0
#define GFP_KERNEL 0
#define false_ 0
#define IS_ENABLED(x) 0
#define S_IRUGO 0
#define SPINWAIT(c,us) do { uint _t=0; while((c) && _t<(us)){udelay(10);_t+=10;} } while(0)
