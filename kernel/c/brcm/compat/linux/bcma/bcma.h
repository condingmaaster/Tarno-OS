#pragma once
#include <linux/types.h>
#include <ssb_sprom.h>
#define BCMA_HOSTTYPE_PCI 1
struct bcma_device; struct bcma_bus;
struct bcma_drv_cc { struct bcma_device *core; };
struct bcma_chipinfo_ { u16 id; u8 rev; u8 pkg; };
struct bcma_bus { int hosttype; struct ssb_sprom sprom; struct bcma_drv_cc drv_cc; struct bcma_chipinfo_ chipinfo; };
struct bcma_device { struct bcma_bus *bus; u16 id_rev; unsigned long mmio; };
u32 bcma_chipco_gpio_control(struct bcma_drv_cc*, u32, u32); u32 bcma_chipco_gpio_out(struct bcma_drv_cc*, u32, u32); u32 bcma_chipco_gpio_outen(struct bcma_drv_cc*, u32, u32);
void bcma_pmu_spuravoid_pllupdate(struct bcma_drv_cc*, int);
void bcma_chipco_chipctl_maskset(struct bcma_drv_cc*, u32, u32, u32);
void bcma_cc_set32(struct bcma_drv_cc*, u16, u32); void bcma_cc_mask32(struct bcma_drv_cc*, u16, u32); u32 bcma_cc_read32(struct bcma_drv_cc*, u16); void bcma_cc_write32(struct bcma_drv_cc*, u16, u32);
void bcma_cc_maskset32(struct bcma_drv_cc*, u16, u32, u32);

u32 bcma_read32(struct bcma_device*, u16); u16 bcma_read16(struct bcma_device*, u16);
void bcma_write32(struct bcma_device*, u16, u32); void bcma_write16(struct bcma_device*, u16, u16);
void bcma_mask32(struct bcma_device*, u16, u32); void bcma_set32(struct bcma_device*, u16, u32);
void bcma_mask16(struct bcma_device*, u16, u16); void bcma_set16(struct bcma_device*, u16, u16);
u32 bcma_aread32(struct bcma_device*, u16); void bcma_awrite32(struct bcma_device*, u16, u32);
