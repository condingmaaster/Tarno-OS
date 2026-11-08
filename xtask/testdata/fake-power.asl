DefinitionBlock ("", "SSDT", 2, "THOS", "FAKEPWR", 1)
{
    Scope (\_SB)
    {
        Device (AC0)
        {
            Name (_HID, "ACPI0003")
            Method (_PSR, 0) { Return (One) }
        }
        Device (BAT0)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Method (_STA, 0) { Return (0x1F) }
            Method (_BIF, 0)
            {
                Return (Package () { 0, 50000, 40000, 1, 11100, 4000, 2000, 100, 100, "FAKEBAT", "0001", "Li-ion", "THOS" })
            }
            Method (_BST, 0)
            {
                Return (Package () { 1, 8000, 30000, 11000 })
            }
        }
        Device (GFX0)
        {
            Name (_ADR, 0x00020000)
            Device (LCD0)
            {
                Name (_ADR, 0x0400)
                Name (LVL, 70)
                Method (_BCL, 0) { Return (Package () { 80, 40, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }) }
                Method (_BCM, 1) { Store (Arg0, LVL) }
                Method (_BQC, 0) { Return (LVL) }
            }
        }
        Device (LID0)
        {
            Name (_HID, EisaId ("PNP0C0D"))
            Method (_LID, 0) { Return (One) }
        }
    }
    ThermalZone (\_TZ.TZ00)
    {
        Method (_TMP, 0) { Return (3281) }
    }
}
