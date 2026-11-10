# WLAN BCM43225 (brcmsmac port) — progress log

Reference: Linux 6.12 `drivers/net/wireless/broadcom/brcm80211/brcmsmac` (ISC), `drivers/bcma`. Copies for reading: not stored in the repo.
Test only on the real Acer (QEMU has no such chip). Driver steps are triggered through `/dev/wlan` (root) so the boot never touches the chip:

    echo cc > /dev/wlan      # ChipCommon + PMU readout, PMU control bit
    echo sprom > /dev/wlan   # SPROM dump (board data, MAC)
    echo core > /dev/wlan    # enable D11 core (PHY clock)
    echo ucode > /dev/wlan   # upload MIMO16 microcode, run PSM until it self-suspends, write 485 init values

Results appear in the kernel log (netconsole).

## Done (verified on the Acer, 2026-11-10)
1. ChipCommon/PMU readable (pmu rev 6, no resource masks needed for 43225), SPROM valid (r8 layout).
2. D11 core enables; machwcap 0xb0a04805, phyversion 0x8406.
3. Microcode upload verified by readback; PSM self-suspends after 1 ms; init values written.

## Next
4. N-PHY: reset + init (phy_n.c, phytbl_n.c tables ~10k lines, radio 2057 init), SPROM board parameters, channel set.
5. DMA rings (dma.c): RX descriptors + TX FIFO 0 (DMA64).
6. Scan: probe request TX, beacon/probe response RX → list of SSIDs.
7. 802.11 auth/assoc, WPA2-PSK (PBKDF2, 4-way handshake, CCMP via hardware keys or software), DHCP via existing net stack.
