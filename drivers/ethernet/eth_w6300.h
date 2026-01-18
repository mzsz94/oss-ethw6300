#ifndef ETH_W6300_H_
#define ETH_W6300_H_

#include <stdint.h>
#include <zephyr/device.h>

/* W6300 common registers used for basic bring-up checks. */
#define W6300_MR		0x0000
#define W6300_PHYCFGR		0x002E
#define W6300_VERSIONR		0x0039

/* PHY configuration bits. */
#define W6300_PHYCFGR_LNK	0x01
#define W6300_PHYCFGR_SPD	0x02
#define W6300_PHYCFGR_DPX	0x04

int eth_w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t *val);

#endif /* ETH_W6300_H_ */
