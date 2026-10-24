/* BrysOS: lo que e1000 toma de e1000_ethtool.c, que no se compila (sirve a la herramienta
 * ethtool de Linux, que BrysOS no tiene). Sólo la lectura de la EEPROM, que e1000_main.c usa
 * para mostrarla si su suma de comprobación es incorrecta. */
#include "../drivers/net/e1000/e1000.h"

static int brys_e1000_get_eeprom_len(struct net_device* netdev) {
    struct e1000_adapter* adapter = netdev_priv(netdev);
    return adapter->hw.eeprom.word_size * 2;
}

static int brys_e1000_get_eeprom(struct net_device* netdev, struct ethtool_eeprom* eeprom, u8* bytes) {
    struct e1000_adapter* adapter = netdev_priv(netdev);
    u16 word;
    for (u32 i = 0; i < eeprom->len; i++) {
        u32 off = eeprom->offset + i;
        if (e1000_read_eeprom(&adapter->hw, (u16)(off >> 1), 1, &word)) return -EIO;
        bytes[i] = (u8)(off & 1 ? word >> 8 : word);
    }
    return 0;
}

static const struct ethtool_ops brys_e1000_ethtool_ops = {
    .get_eeprom_len = brys_e1000_get_eeprom_len,
    .get_eeprom = brys_e1000_get_eeprom,
};

void e1000_set_ethtool_ops(struct net_device* netdev) { netdev->ethtool_ops = &brys_e1000_ethtool_ops; }
