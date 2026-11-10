// SPDX-License-Identifier: GPL-2.0-or-later
//! EHCI QH endpoint capabilities (EHCI 3.6.2 / Linux ehci.h).
//! Hub address is bits 16..22; downstream port is bits 23..29.
pub const fn queue_caps(hub_address:u8,port:u8,microframe_masks:u16)->u32 {
    (1<<30)|(((hub_address&0x7f) as u32)<<16)|(((port&0x7f) as u32)<<23)|microframe_masks as u32
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn root_has_no_transaction_translator() {assert_eq!(queue_caps(0,0,0),0x40000000);}
    #[test]
    fn distinct_hub_and_port_fields_match_hardware_layout() {
        assert_eq!(queue_caps(9,3,0),0x41890000);
        assert_eq!(queue_caps(5,2,0x1c01),0x41051c01);
        assert_eq!(queue_caps(127,127,0xffff),0x7fffffff);
    }
}
