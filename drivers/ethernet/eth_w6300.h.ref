#ifndef ETH_W6300_H_
#define ETH_W6300_H_

#include <stdint.h>
#include <zephyr/device.h>

/* W6300 Common Registers (offsets used with INSTR/ADDR/DUMMY/DATA frame) */
#define W6300_CIDR0		0x0000	/* Chip ID high byte (expected 0x63) */
#define W6300_CIDR1		0x0001	/* Chip ID low byte (expected 0x00) */
#define W6300_VER		0x0002	/* Version */
#define W6300_SYSR		0x2000	/* System Status */
#define W6300_PHYSR		0x3000	/* PHY Status Register */

/* PHY Status bits (PHYSR). */
#define W6300_PHYSR_LNK		0x01	/* Link (1=up, 0=down) */
#define W6300_PHYSR_SPD		0x02	/* Speed (1=100M, 0=10M) */
#define W6300_PHYSR_DPX		0x04	/* Duplex (1=Full, 0=Half) */

int eth_w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t *val);
int eth_w6300_write_reg(const struct device *dev, uint16_t addr, uint8_t val);

#endif /* ETH_W6300_H_ */
