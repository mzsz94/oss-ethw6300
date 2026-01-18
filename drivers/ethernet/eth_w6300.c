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

#define W6300_SPI_MOD_SINGLE 0x00
#define W6300_SPI_RWB_READ 0x00
#define W6300_SPI_RWB_WRITE 0x01
#define W6300_SPI_BSB_COMMON 0x00
#define W6300_SPI_INSTR(mod, rwb, bsb) \
	((uint8_t)((((mod) & 0x3) << 6) | (((rwb) & 0x1) << 5) | ((bsb) & 0x1F)))

struct eth_w6300_config {
	struct gpio_dt_spec cs_gpio;
	struct gpio_dt_spec sclk_gpio;
	struct gpio_dt_spec mosi_gpio;
	struct gpio_dt_spec miso_gpio;
	struct gpio_dt_spec io2_gpio;
	struct gpio_dt_spec io3_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec interrupt;
};

struct eth_w6300_dev_data {
	struct net_if *iface;
	uint8_t mac_addr[6];
};

static void w6300_spi_delay(void)
{
	k_busy_wait(10);
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
	/* Mode 0: idle low, change data on falling edge, sample on rising edge. */
	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&cfg->mosi_gpio, (data >> i) & 0x01);
		w6300_spi_delay();
		/* Rising edge clocks data into the W6300. */
		gpio_pin_set_dt(&cfg->sclk_gpio, 1);
		w6300_spi_delay();
		gpio_pin_set_dt(&cfg->sclk_gpio, 0);
		w6300_spi_delay();
	}
}

static uint8_t w6300_spi_read_byte(const struct eth_w6300_config *cfg)
{
	uint8_t data = 0;

	for (int i = 7; i >= 0; i--) {
		/* Sample MISO on rising edge (Mode 0). */
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

static inline uint8_t w6300_spi_instr(uint8_t rwb, uint8_t bsb)
{
	return W6300_SPI_INSTR(W6300_SPI_MOD_SINGLE, rwb, bsb);
}

/*
 * W6300 single-SPI frame:
 *   [INSTR(1B)] [ADDR(2B)] [DUMMY(1B)] [DATA...]
 *
 * INSTR: MOD[7:6], RWB[5], BSB[4:0]. Do not use W5x00 control/VDM framing.
 */
static void w6300_spi_write_header(const struct eth_w6300_config *cfg, uint8_t instr,
				   uint16_t addr)
{
	w6300_spi_write_byte(cfg, instr);
	w6300_spi_write_byte(cfg, (addr >> 8) & 0xFF);
	w6300_spi_write_byte(cfg, addr & 0xFF);
	w6300_spi_write_byte(cfg, 0x00);
}

int eth_w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t *val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t instr = w6300_spi_instr(W6300_SPI_RWB_READ, W6300_SPI_BSB_COMMON);

	w6300_cs_assert(cfg);
	w6300_spi_write_header(cfg, instr, addr);
	*val = w6300_spi_read_byte(cfg);
	w6300_cs_deassert(cfg);
	return 0;
}

int eth_w6300_write_reg(const struct device *dev, uint16_t addr, uint8_t val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t instr = w6300_spi_instr(W6300_SPI_RWB_WRITE, W6300_SPI_BSB_COMMON);

	w6300_cs_assert(cfg);
	w6300_spi_write_header(cfg, instr, addr);
	w6300_spi_write_byte(cfg, val);
	w6300_cs_deassert(cfg);
	return 0;
}

static void w6300_log_phy(uint8_t physr)
{
	const char *link = (physr & W6300_PHYSR_LNK) ? "up" : "down";
	const char *speed = (physr & W6300_PHYSR_SPD) ? "100" : "10";
	const char *duplex = (physr & W6300_PHYSR_DPX) ? "full" : "half";

	LOG_INF("W6300 PHY: link %s, %s Mbps, %s duplex (PHYSR=0x%02X)",
		link, speed, duplex, physr);
}

static int eth_w6300_init(const struct device *dev)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t reg = 0;
	int ret;

	if (cfg->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure reset GPIO");
			return ret;
		}
	}

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

	/* CSn is active-low on W6300-EVB-Pico2; flags handle inversion. */
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
	gpio_pin_set_dt(&cfg->sclk_gpio, 0);
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

	if (cfg->io2_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->io2_gpio)) {
			LOG_ERR("IO2 GPIO not ready");
			return -ENODEV;
		}
		/* IO2 is QSPI-only; keep high-Z in single SPI mode. */
		ret = gpio_pin_configure_dt(&cfg->io2_gpio, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Failed to configure IO2 GPIO");
			return ret;
		}
	}

	if (cfg->io3_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->io3_gpio)) {
			LOG_ERR("IO3 GPIO not ready");
			return -ENODEV;
		}
		/* IO3 is QSPI-only; keep high-Z in single SPI mode. */
		ret = gpio_pin_configure_dt(&cfg->io3_gpio, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Failed to configure IO3 GPIO");
			return ret;
		}
	}

	if (cfg->reset_gpio.port != NULL) {
		/* RSTn is active-low on W6300-EVB-Pico2. */
		k_msleep(10);
		gpio_pin_set_dt(&cfg->reset_gpio, 0);
		k_msleep(200);
	}

	LOG_INF("W6300 Driver Initializing...");

	{
		uint8_t cidr0 = 0;
		uint8_t cidr1 = 0;

		if (eth_w6300_read_reg(dev, W6300_CIDR0, &cidr0) == 0) {
			LOG_INF("W6300 CIDR0=0x%02X", cidr0);
		}
		if (eth_w6300_read_reg(dev, W6300_CIDR1, &cidr1) == 0) {
			LOG_INF("W6300 CIDR1=0x%02X", cidr1);
		}
	}
	if (eth_w6300_read_reg(dev, W6300_VER, &reg) == 0) {
		LOG_INF("W6300 VER=0x%02X", reg);
	}
	if (eth_w6300_read_reg(dev, W6300_PHYSR, &reg) == 0) {
		w6300_log_phy(reg);
	}

	if (eth_w6300_read_reg(dev, W6300_SYSR, &reg) == 0) {
		LOG_INF("W6300 SYSR=0x%02X", reg);
	}

	LOG_INF("W6300 register dump 0x0000-0x000F:");
	uint8_t prev = 0;
	int same_run = 0;
	int max_run = 0;
	bool prev_valid = false;

	for (int i = 0; i < 0x10; i++) {
		uint8_t val = 0;

		if (eth_w6300_read_reg(dev, (uint16_t)i, &val) == 0) {
			LOG_INF("  [0x%04X] = 0x%02X", i, val);
			if (prev_valid && val == prev) {
				same_run++;
			} else {
				same_run = 1;
				prev = val;
				prev_valid = true;
			}
			if (same_run > max_run) {
				max_run = same_run;
			}
		}
	}
	if (max_run >= 8) {
		LOG_WRN("Many identical register reads; possible SPI framing or MISO issue");
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
		.io2_gpio = GPIO_DT_SPEC_INST_GET_OR(n, io2_gpios, {0}), \
		.io3_gpio = GPIO_DT_SPEC_INST_GET_OR(n, io3_gpios, {0}), \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {0}), \
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
