#pragma once
#include <linux/types.h>
void udelay(unsigned long us); void mdelay(unsigned long ms); void usleep_range(unsigned long a, unsigned long b);
#define IEEE80211_STYPE_PROBE_REQ 0x0040
struct ieee80211_rts { __le16 frame_control; __le16 duration; u8 ra[6]; u8 ta[6]; } __packed;
struct ieee80211_hdr { __le16 frame_control; __le16 duration_id; u8 addr1[6]; u8 addr2[6]; u8 addr3[6]; __le16 seq_ctrl; } __packed;
