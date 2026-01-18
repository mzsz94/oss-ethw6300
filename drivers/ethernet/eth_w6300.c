#define DT_DRV_COMPAT wiznet_w6300

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(eth_w6300, CONFIG_ETHERNET_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/ethernet.h>

#include "eth_w6300.h"

struct eth_w6300_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec interrupt;
};

struct eth_w6300_dev_data {
	struct net_if *iface;
	uint8_t mac_addr[6];
};

/* 
 * Basic SPI Read function for Wiznet Chips (Variable Data Length) 
 * Frame: [Addr High][Addr Low][Control][Data...] 
 * Note: Control byte varies by chip. Assuming 0x00 for Read/Common Block for now.
 */
int eth_w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t *val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t tx_data[4];
	uint8_t rx_data[4] = { 0 };

	/* W6300/W6100 Control Byte might differ. 
	 * For now, just trying to send 3 bytes (Addr+Ctrl) and read 1 byte.
	 * Addr: 16bit, Control: 8bit.
	 */
	tx_data[0] = (addr >> 8) & 0xFF;
	tx_data[1] = addr & 0xFF;
	tx_data[2] = 0x00; /* Control Byte - To be verified */
	tx_data[3] = 0x00; /* Dummy byte to clock in data */

	const struct spi_buf tx_buf = {
		.buf = tx_data,
		.len = sizeof(tx_data),
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	struct spi_buf rx_buf = {
		.buf = rx_data,
		.len = sizeof(rx_data),
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1,
	};

	int ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
	if (ret < 0) {
		LOG_ERR("SPI transfer failed: %d", ret);
		return ret;
	}

	*val = rx_data[3];
	return 0;
}

static void w6300_log_phy(uint8_t phy)
{
	const char *link = (phy & W6300_PHYCFGR_LNK) ? "up" : "down";
	const char *speed = (phy & W6300_PHYCFGR_SPD) ? "100" : "10";
	const char *duplex = (phy & W6300_PHYCFGR_DPX) ? "full" : "half";

	LOG_INF("W6300 PHY: link %s, %s Mbps, %s duplex (PHYCFGR=0x%02X)",
		link, speed, duplex, phy);
}

static int eth_w6300_init(const struct device *dev)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t reg = 0;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus %s not ready", cfg->spi.bus->name);
		return -ENODEV;
	}

	LOG_INF("W6300 Driver Initializing...");

	if (eth_w6300_read_reg(dev, W6300_MR, &reg) == 0) {
		LOG_INF("W6300 MR=0x%02X", reg);
	}
	if (eth_w6300_read_reg(dev, W6300_VERSIONR, &reg) == 0) {
		LOG_INF("W6300 VERSIONR=0x%02X", reg);
	}
	if (eth_w6300_read_reg(dev, W6300_PHYCFGR, &reg) == 0) {
		w6300_log_phy(reg);
	}

	return 0;
}

static const struct ethernet_api eth_w6300_api = {
	/* We will fill this later */
};

#define ETH_W6300_DEFINE(n) \
	static const struct eth_w6300_config eth_w6300_config_##n = { \
		.spi = SPI_DT_SPEC_INST_GET(n, SPI_WORD_SET(8), 0), \
		.interrupt = GPIO_DT_SPEC_INST_GET(n, int_gpios), \
	}; \
	\
	static struct eth_w6300_dev_data eth_w6300_data_##n; \
	\
	DEVICE_DT_INST_DEFINE(n, \
		eth_w6300_init, \
		NULL, \
		&eth_w6300_data_##n, \
		&eth_w6300_config_##n, \
		POST_KERNEL, \
		CONFIG_ETH_INIT_PRIORITY, \
		&eth_w6300_api);

DT_INST_FOREACH_STATUS_OKAY(ETH_W6300_DEFINE)
