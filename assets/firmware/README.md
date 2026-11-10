# Acer device firmware

Unmodified files from the official Linux Firmware release `20251021`:
https://gitlab.com/kernel-firmware/linux-firmware/-/tree/20251021

- `brcm/bcm43xx-0.fw` and `brcm/bcm43xx_hdr-0.fw`: BCM43225/brcmsmac bundle,
  under the accompanying `LICENCE.broadcom_bcm43xx`.
- `radeon/CEDAR_me.bin`, `CEDAR_pfp.bin`, `CEDAR_rlc.bin`: Radeon Cedar/HD 5470,
  under the accompanying `LICENSE.radeon`.

These binaries have their own licenses; the THOS source-code GPL does not
replace them. Both full license texts ship alongside the firmware in the image.
Broadcom firmware is for its intended Broadcom products only. The binaries have
not been modified, disassembled or reverse engineered.

`SHA256SUMS` records the downloaded release files and license texts. The image
builder checks all hashes and the brcmsmac index/profile before copying this
directory to `/lib/firmware`. Kernel checks report availability separately from
upload/activation; firmware availability is not an operational device driver.
