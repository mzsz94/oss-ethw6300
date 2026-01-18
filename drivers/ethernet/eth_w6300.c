#define DT_DRV_COMPAT wiznet_w6300

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(eth_w6300, CONFIG_ETHERNET_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/ethernet.h>

#include "eth_w6300.h"

#define W6300_SPI_READ 0x00
#define W6300_SPI_VDM 0x00

struct eth_w6300_config {
	struct gpio_dt_spec cs_gpio;
	struct gpio_dt_spec sclk_gpio;
	struct gpio_dt_spec mosi_gpio;
	struct gpio_dt_spec miso_gpio;
	struct gpio_dt_spec interrupt;
};

struct eth_w6300_dev_data {
	struct net_if *iface;
	uint8_t mac_addr[6];
};

static void w6300_spi_delay(void)
{
	k_busy_wait(1);
}

static void w6300_cs_assert(const struct eth_w6300_config *cfg)
{
	gpio_pin_set_dt(&cfg->cs_gpio, 1);
}

static void w6300_cs_deassert(const struct eth_w6300_config *cfg)
{
	gpio_pin_set_dt(&cfg->cs_gpio, 0);
}

static void w6300_spi_write_byte(const struct eth_w6300_config *cfg, uint8_t data)
{
	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&cfg->mosi_gpio, (data >> i) & 0x01);
		w6300_spi_delay();
		gpio_pin_set_dt(&cfg->sclk_gpio, 1);
		w6300_spi_delay();
		gpio_pin_set_dt(&cfg->sclk_gpio, 0);
	}
}

static uint8_t w6300_spi_read_byte(const struct eth_w6300_config *cfg)
{
	uint8_t data = 0;

	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&cfg->sclk_gpio, 1);
		w6300_spi_delay();
		if (gpio_pin_get_dt(&cfg->miso_gpio)) {
			data |= (1U << i);
		}
		gpio_pin_set_dt(&cfg->sclk_gpio, 0);
		w6300_spi_delay();
	}

	return data;
}

int eth_w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t *val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t control;

	control = ((addr >> 16) & 0x1F) << 3;
	control |= W6300_SPI_READ;
	control |= W6300_SPI_VDM;

	w6300_cs_assert(cfg);
	w6300_spi_write_byte(cfg, (addr >> 8) & 0xFF);
	w6300_spi_write_byte(cfg, addr & 0xFF);
	w6300_spi_write_byte(cfg, control);
	*val = w6300_spi_read_byte(cfg);
	w6300_cs_deassert(cfg);
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
	int ret;

	if (!gpio_is_ready_dt(&cfg->cs_gpio)) {
		LOG_ERR("CS GPIO not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->sclk_gpio)) {
		LOG_ERR("SCLK GPIO not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->mosi_gpio)) {
		LOG_ERR("MOSI GPIO not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->miso_gpio)) {
		LOG_ERR("MISO GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure CS GPIO");
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->sclk_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure SCLK GPIO");
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->mosi_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure MOSI GPIO");
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->miso_gpio, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure MISO GPIO");
		return ret;
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

#define W6300_SPI_BUS(inst) DT_BUS(DT_DRV_INST(inst))

#define ETH_W6300_DEFINE(n) \
	static const struct eth_w6300_config eth_w6300_config_##n = { \
		.cs_gpio = SPI_CS_GPIOS_DT_SPEC_INST_GET(n), \
		.sclk_gpio = GPIO_DT_SPEC_GET_BY_IDX(W6300_SPI_BUS(n), \
						     clk_gpios, 0), \
		.mosi_gpio = GPIO_DT_SPEC_GET_BY_IDX(W6300_SPI_BUS(n), \
						     mosi_gpios, 0), \
		.miso_gpio = GPIO_DT_SPEC_GET_BY_IDX(W6300_SPI_BUS(n), \
						     miso_gpios, 0), \
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
