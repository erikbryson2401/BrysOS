/* BrysOS: ethtool (la herramienta de configuración de Linux no existe en BrysOS: sólo las
 * constantes y estructuras que nombran los drivers) */
#ifndef _BRYS_LINUX_ETHTOOL_H
#define _BRYS_LINUX_ETHTOOL_H
#include <linux/types.h>
#define SPEED_10 10
#define SPEED_100 100
#define SPEED_1000 1000
#define SPEED_UNKNOWN -1
#define DUPLEX_HALF 0x00
#define DUPLEX_FULL 0x01
#define DUPLEX_UNKNOWN 0xff
#define AUTONEG_DISABLE 0x00
#define AUTONEG_ENABLE 0x01
#define ETH_TP_MDI_INVALID 0x00
#define ETH_TP_MDI 0x01
#define ETH_TP_MDI_X 0x02
#define ETH_TP_MDI_AUTO 0x03
#define ETH_GSTRING_LEN 32
#define WAKE_PHY (1 << 0)
#define WAKE_UCAST (1 << 1)
#define WAKE_MCAST (1 << 2)
#define WAKE_BCAST (1 << 3)
#define WAKE_ARP (1 << 4)
#define WAKE_MAGIC (1 << 5)
struct net_device;
struct ethtool_eeprom { u32 cmd; u32 magic; u32 offset; u32 len; u8 data[]; };
/* Sólo las operaciones que usa el código portado fuera de los archivos de ethtool */
struct ethtool_ops {
    int (*get_eeprom_len)(struct net_device* dev);
    int (*get_eeprom)(struct net_device* dev, struct ethtool_eeprom* ee, u8* data);
};
struct ethtool_drvinfo { char driver[32]; char version[32]; char fw_version[32]; char bus_info[32]; };
#endif
