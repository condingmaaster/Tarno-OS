struct ssb_sprom_core_pwr_info {
	u8 itssi_2g, itssi_5g;
	u8 maxpwr_2g, maxpwr_5gl, maxpwr_5g, maxpwr_5gh;
	u16 pa_2g[4], pa_5gl[4], pa_5g[4], pa_5gh[4];
};
struct ssb_sprom {
	u8 revision;
	u8 il0mac[6] __aligned(sizeof(u16));	/* MAC address for 802.11b/g */
	u8 et0mac[6] __aligned(sizeof(u16));	/* MAC address for Ethernet */
	u8 et1mac[6] __aligned(sizeof(u16));	/* MAC address for 802.11a */
	u8 et2mac[6] __aligned(sizeof(u16));	/* MAC address for extra Ethernet */
	u8 et0phyaddr;		/* MII address for enet0 */
	u8 et1phyaddr;		/* MII address for enet1 */
	u8 et2phyaddr;		/* MII address for enet2 */
	u8 et0mdcport;		/* MDIO for enet0 */
	u8 et1mdcport;		/* MDIO for enet1 */
	u8 et2mdcport;		/* MDIO for enet2 */
	u16 dev_id;		/* Device ID overriding e.g. PCI ID */
	u16 board_rev;		/* Board revision number from SPROM. */
	u16 board_num;		/* Board number from SPROM. */
	u16 board_type;		/* Board type from SPROM. */
	u8 country_code;	/* Country Code */
	char alpha2[2];		/* Country Code as two chars like EU or US */
	u8 leddc_on_time;	/* LED Powersave Duty Cycle On Count */
	u8 leddc_off_time;	/* LED Powersave Duty Cycle Off Count */
	u8 ant_available_a;	/* 2GHz antenna available bits (up to 4) */
	u8 ant_available_bg;	/* 5GHz antenna available bits (up to 4) */
	u16 pa0b0;
	u16 pa0b1;
	u16 pa0b2;
	u16 pa1b0;
	u16 pa1b1;
	u16 pa1b2;
	u16 pa1lob0;
	u16 pa1lob1;
	u16 pa1lob2;
	u16 pa1hib0;
	u16 pa1hib1;
	u16 pa1hib2;
	u8 gpio0;		/* GPIO pin 0 */
	u8 gpio1;		/* GPIO pin 1 */
	u8 gpio2;		/* GPIO pin 2 */
	u8 gpio3;		/* GPIO pin 3 */
	u8 maxpwr_bg;		/* 2.4GHz Amplifier Max Power (in dBm Q5.2) */
	u8 maxpwr_al;		/* 5.2GHz Amplifier Max Power (in dBm Q5.2) */
	u8 maxpwr_a;		/* 5.3GHz Amplifier Max Power (in dBm Q5.2) */
	u8 maxpwr_ah;		/* 5.8GHz Amplifier Max Power (in dBm Q5.2) */
	u8 itssi_a;		/* Idle TSSI Target for A-PHY */
	u8 itssi_bg;		/* Idle TSSI Target for B/G-PHY */
	u8 tri2g;		/* 2.4GHz TX isolation */
	u8 tri5gl;		/* 5.2GHz TX isolation */
	u8 tri5g;		/* 5.3GHz TX isolation */
	u8 tri5gh;		/* 5.8GHz TX isolation */
	u8 txpid2g[4];		/* 2GHz TX power index */
	u8 txpid5gl[4];		/* 4.9 - 5.1GHz TX power index */
	u8 txpid5g[4];		/* 5.1 - 5.5GHz TX power index */
	u8 txpid5gh[4];		/* 5.5 - ...GHz TX power index */
	s8 rxpo2g;		/* 2GHz RX power offset */
	s8 rxpo5g;		/* 5GHz RX power offset */
	u8 rssisav2g;		/* 2GHz RSSI params */
	u8 rssismc2g;
	u8 rssismf2g;
	u8 bxa2g;		/* 2GHz BX arch */
	u8 rssisav5g;		/* 5GHz RSSI params */
	u8 rssismc5g;
	u8 rssismf5g;
	u8 bxa5g;		/* 5GHz BX arch */
	u16 cck2gpo;		/* CCK power offset */
	u32 ofdm2gpo;		/* 2.4GHz OFDM power offset */
	u32 ofdm5glpo;		/* 5.2GHz OFDM power offset */
	u32 ofdm5gpo;		/* 5.3GHz OFDM power offset */
	u32 ofdm5ghpo;		/* 5.8GHz OFDM power offset */
	u32 boardflags;
	u32 boardflags2;
	u32 boardflags3;
	/* TODO: Switch all drivers to new u32 fields and drop below ones */
	u16 boardflags_lo;	/* Board flags (bits 0-15) */
	u16 boardflags_hi;	/* Board flags (bits 16-31) */
	u16 boardflags2_lo;	/* Board flags (bits 32-47) */
	u16 boardflags2_hi;	/* Board flags (bits 48-63) */

	struct ssb_sprom_core_pwr_info core_pwr_info[4];

	/* Antenna gain values for up to 4 antennas
	 * on each band. Values in dBm/4 (Q5.2). Negative gain means the
	 * loss in the connectors is bigger than the gain. */
	struct {
		s8 a0, a1, a2, a3;
	} antenna_gain;

	struct {
		struct {
			u8 tssipos, extpa_gain, pdet_range, tr_iso, antswlut;
		} ghz2;
		struct {
			u8 tssipos, extpa_gain, pdet_range, tr_iso, antswlut;
		} ghz5;
	} fem;

	u16 mcs2gpo[8];
	u16 mcs5gpo[8];
	u16 mcs5glpo[8];
	u16 mcs5ghpo[8];
	u8 opo;

	u8 rxgainerr2ga[3];
	u8 rxgainerr5gla[3];
	u8 rxgainerr5gma[3];
	u8 rxgainerr5gha[3];
	u8 rxgainerr5gua[3];

	u8 noiselvl2ga[3];
	u8 noiselvl5gla[3];
	u8 noiselvl5gma[3];
	u8 noiselvl5gha[3];
	u8 noiselvl5gua[3];

	u8 regrev;
	u8 txchain;
	u8 rxchain;
	u8 antswitch;
	u16 cddpo;
	u16 stbcpo;
	u16 bw40po;
	u16 bwduppo;

	u8 tempthresh;
	u8 tempoffset;
	u16 rawtempsense;
	u8 measpower;
	u8 tempsense_slope;
	u8 tempcorrx;
	u8 tempsense_option;
	u8 freqoffset_corr;
	u8 iqcal_swp_dis;
	u8 hw_iqcal_en;
	u8 elna2g;
	u8 elna5g;
	u8 phycal_tempdelta;
	u8 temps_period;
	u8 temps_hysteresis;
	u8 measpower1;
	u8 measpower2;
	u8 pcieingress_war;

	/* power per rate from sromrev 9 */
	u16 cckbw202gpo;
	u16 cckbw20ul2gpo;
	u32 legofdmbw202gpo;
	u32 legofdmbw20ul2gpo;
	u32 legofdmbw205glpo;
	u32 legofdmbw20ul5glpo;
	u32 legofdmbw205gmpo;
	u32 legofdmbw20ul5gmpo;
	u32 legofdmbw205ghpo;
	u32 legofdmbw20ul5ghpo;
	u32 mcsbw202gpo;
	u32 mcsbw20ul2gpo;
	u32 mcsbw402gpo;
	u32 mcsbw205glpo;
	u32 mcsbw20ul5glpo;
	u32 mcsbw405glpo;
	u32 mcsbw205gmpo;
	u32 mcsbw20ul5gmpo;
	u32 mcsbw405gmpo;
	u32 mcsbw205ghpo;
	u32 mcsbw20ul5ghpo;
	u32 mcsbw405ghpo;
	u16 mcs32po;
	u16 legofdm40duppo;
	u8 sar2g;
	u8 sar5g;
};
