#pragma once
#include <linux/types.h>
void udelay(unsigned long us); void mdelay(unsigned long ms); void usleep_range(unsigned long a, unsigned long b);
struct sk_buff { struct sk_buff *next, *prev; unsigned char *data; unsigned int len; unsigned char cb[48]; };
struct sk_buff_head { struct sk_buff *next, *prev; u32 qlen; };
int skb_queue_empty(const struct sk_buff_head*); struct sk_buff *skb_peek(const struct sk_buff_head*); struct sk_buff *skb_peek_tail(const struct sk_buff_head*);
