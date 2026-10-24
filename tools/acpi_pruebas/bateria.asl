/* SSDT de prueba para QEMU (que no tiene batería): una batería que pierde 1 % cada vez que se
 * lee su estado, un cargador desconectado y el brillo de la pantalla en la tarjeta gráfica de
 * QEMU (00:01.0). Se compila con iasl y se pasa con -acpitable file=... */
DefinitionBlock ("", "SSDT", 2, "BRYSOS", "BATERIA", 1)
{
    External (\_SB.PCI0, DeviceObj)

    Scope (\_SB)
    {
        Device (ADP1)
        {
            Name (_HID, "ACPI0003")
            Method (_PSR, 0) { Return (Zero) }
        }

        Device (BAT1)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, One)
            Name (REM, 4400)
            Method (_STA, 0) { Return (0x1F) }
            Method (_BIF, 0)
            {
                Return (Package (13) {
                    One,            // mAh
                    4800,           // capacidad de diseño
                    4400,           // última carga completa
                    One,            // recargable
                    10800,          // tensión de diseño (mV)
                    440, 220, 44, 44,
                    "PRUEBA-6C", "123", "LION", "BrysOS"
                })
            }
            Method (_BST, 0)
            {
                If (REM > 44) { REM -= 44 }
                Return (Package (4) { One, 1100, REM, 11100 })
            }
        }
    }

    Scope (\_SB.PCI0)
    {
        Device (GFX0)
        {
            Name (_ADR, 0x00010000)
            Device (LCD0)
            {
                Name (_ADR, 0x0110)
                Name (BRT, 60)
                Method (_BCL, 0)
                {
                    Return (Package () { 100, 60, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 })
                }
                Method (_BCM, 1) { BRT = Arg0 }
                Method (_BQC, 0) { Return (BRT) }
            }
        }
    }
}
