/* BrysOS: registros MII de los PHY Ethernet (subconjunto de uapi/linux/mii.h) */
#ifndef _BRYS_LINUX_MII_H
#define _BRYS_LINUX_MII_H
#include <linux/types.h>
#define MII_BMCR 0x00
#define MII_BMSR 0x01
#define MII_PHYSID1 0x02
#define MII_PHYSID2 0x03
#define MII_ADVERTISE 0x04
#define MII_LPA 0x05
#define MII_CTRL1000 0x09
#define MII_STAT1000 0x0a
#define BMCR_RESET 0x8000
#define BMCR_ANENABLE 0x1000
#define BMCR_ANRESTART 0x0200
#define BMCR_FULLDPLX 0x0100
#define BMCR_SPEED100 0x2000
#define BMCR_SPEED1000 0x0040
#define BMSR_LSTATUS 0x0004
#define SIOCGMIIPHY 0x8947
#define SIOCGMIIREG 0x8948
#define SIOCSMIIREG 0x8949
struct mii_ioctl_data { u16 phy_id; u16 reg_num; u16 val_in; u16 val_out; };
struct ifreq;
struct mii_ioctl_data* if_mii(struct ifreq* rq);
#endif
