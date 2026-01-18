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
 * 	[INSTR(1B)] [ADDR(2B)] [DUMMY(1B)] [DATA...]
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

/* --- RX/TX Implementation --- */

static uint16_t w6300_get_tx_free_size(const struct device *dev)
{
	uint16_t val = 0;
	eth_w6300_read_reg(dev, W6300_REG_Sn_TX_FSR, (uint8_t *)&val); /* High byte */
	/* Warning: Byte order? W6300 is Big Endian. read_reg returns 8bit? No, function is 8bit.
	   Let's use our helper eth_w6300_read_reg which reads 1 byte. 
	   Wait, the reference code provided eth_w6300_read_reg which takes uint16_t addr and returns uint8_t *val.
	   We need a 16-bit read helper.
	*/
	uint8_t h, l;
	eth_w6300_read_reg(dev, W6300_REG_Sn_TX_FSR, &h);
	eth_w6300_read_reg(dev, W6300_REG_Sn_TX_FSR + 1, &l);
	return (h << 8) | l;
}

static uint16_t w6300_get_tx_wr_ptr(const struct device *dev)
{
	uint8_t h, l;
	eth_w6300_read_reg(dev, W6300_REG_Sn_TX_WR, &h);
	eth_w6300_read_reg(dev, W6300_REG_Sn_TX_WR + 1, &l);
	return (h << 8) | l;
}

static void w6300_set_tx_wr_ptr(const struct device *dev, uint16_t ptr)
{
	eth_w6300_write_reg(dev, W6300_REG_Sn_TX_WR, (ptr >> 8) & 0xFF);
	eth_w6300_write_reg(dev, W6300_REG_Sn_TX_WR + 1, ptr & 0xFF);
}

static uint16_t w6300_get_rx_size(const struct device *dev)
{
	uint8_t h, l;
	eth_w6300_read_reg(dev, W6300_REG_Sn_RX_RSR, &h);
	eth_w6300_read_reg(dev, W6300_REG_Sn_RX_RSR + 1, &l);
	return (h << 8) | l;
}

static uint16_t w6300_get_rx_rd_ptr(const struct device *dev)
{
	uint8_t h, l;
	eth_w6300_read_reg(dev, W6300_REG_Sn_RX_RD, &h);
	eth_w6300_read_reg(dev, W6300_REG_Sn_RX_RD + 1, &l);
	return (h << 8) | l;
}

static void w6300_set_rx_rd_ptr(const struct device *dev, uint16_t ptr)
{
	eth_w6300_write_reg(dev, W6300_REG_Sn_RX_RD, (ptr >> 8) & 0xFF);
	eth_w6300_write_reg(dev, W6300_REG_Sn_RX_RD + 1, ptr & 0xFF);
}

static void w6300_write_buf(const struct device *dev, uint16_t addr, uint8_t block, uint8_t *buf, uint16_t len)
{
	const struct eth_w6300_config *cfg = dev->config;
	/* Block is always S0_TX (0x02) for TX buffer */
	/* Frame: [INSTR][ADDR][DATA...] */
	uint8_t instr = w6300_spi_instr(W6300_SPI_RWB_WRITE, W6300_SPI_BSB_COMMON); // WAIT, block selection?
	/* W6300 Single SPI mode addresses memory linearly? 
	   Or do we need to select block in INSTR? 
	   The reference macro W6300_SPI_INSTR takes 'bsb' (Block Select bits).
	   So we must pass W6300_BLOCK_S0_TX to instr.
	*/
	instr = w6300_spi_instr(W6300_SPI_RWB_WRITE, block);

	w6300_cs_assert(cfg);
	w6300_spi_write_header(cfg, instr, addr);
	for(int i=0; i<len; i++) w6300_spi_write_byte(cfg, buf[i]);
	w6300_cs_deassert(cfg);
}

static void w6300_read_buf(const struct device *dev, uint16_t addr, uint8_t block, uint8_t *buf, uint16_t len)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t instr = w6300_spi_instr(W6300_SPI_RWB_READ, block);

	w6300_cs_assert(cfg);
	w6300_spi_write_header(cfg, instr, addr);
	for(int i=0; i<len; i++) buf[i] = w6300_spi_read_byte(cfg);
	w6300_cs_deassert(cfg);
}

static void w6300_exec_cmd(const struct device *dev, uint8_t cmd)
{
	eth_w6300_write_reg(dev, W6300_REG_Sn_CR, cmd);
	uint8_t cr;
	do {
		eth_w6300_read_reg(dev, W6300_REG_Sn_CR, &cr);
	} while (cr != 0);
}

static int eth_w6300_send(const struct device *dev, struct net_pkt *pkt)
{
	uint16_t len = net_pkt_get_len(pkt);
	uint16_t freesize;

	do {
		freesize = w6300_get_tx_free_size(dev);
	} while (freesize < len);

	uint16_t ptr = w6300_get_tx_wr_ptr(dev);
	uint16_t offset = ptr;

	struct net_buf *frag;
	for (frag = pkt->buffer; frag; frag = frag->frags) {
		w6300_write_buf(dev, offset, W6300_BLOCK_S0_TX, frag->data, frag->len);
		offset += frag->len;
	}

	w6300_set_tx_wr_ptr(dev, offset);
	w6300_exec_cmd(dev, W6300_CR_SEND);

	return 0;
}

static void eth_w6300_rx(const struct device *dev)
{
	struct eth_w6300_dev_data *data = dev->data;
	uint16_t rsr = w6300_get_rx_size(dev);
	if (rsr == 0) return;

	uint16_t ptr = w6300_get_rx_rd_ptr(dev);
	uint8_t header[2];
	
	/* Read packet header (2 bytes length) */
	w6300_read_buf(dev, ptr, W6300_BLOCK_S0_RX, header, 2);
	ptr += 2;
	
	uint16_t len = (header[0] << 8) | header[1];
	len -= 2; /* Header includes itself in size? Standard W5500 MACRAW: size includes 2 byte header. */

	struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		w6300_set_rx_rd_ptr(dev, ptr + len);
		w6300_exec_cmd(dev, W6300_CR_RECV);
		return;
	}

	uint16_t offset = ptr;
	struct net_buf *frag;
	for (frag = pkt->buffer; frag; frag = frag->frags) {
		w6300_read_buf(dev, offset, W6300_BLOCK_S0_RX, frag->data, frag->len);
		offset += frag->len;
	}

	if (net_recv_data(data->iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}

	w6300_set_rx_rd_ptr(dev, offset);
	w6300_exec_cmd(dev, W6300_CR_RECV);
}

/* --- Software SPI (Bitbang) Implementation --- */

static void soft_spi_init(const struct device *dev)
{
	const struct eth_w6300_config *cfg = dev->config;

	/* Configure GPIOs */
	gpio_pin_configure_dt(&cfg->cs_gpio, GPIO_OUTPUT_INACTIVE); /* CS High (Inactive) */
	gpio_pin_configure_dt(&cfg->sclk_gpio, GPIO_OUTPUT_INACTIVE); /* CLK Low */
	gpio_pin_configure_dt(&cfg->mosi_gpio, GPIO_OUTPUT_INACTIVE); /* MOSI Low */
	gpio_pin_configure_dt(&cfg->miso_gpio, GPIO_INPUT);
	
	gpio_pin_set_dt(&cfg->cs_gpio, 0); /* Release CS (High) */
}

/* Helper wrappers */
static uint8_t w6300_read_reg8(const struct device *dev, uint16_t offset, uint8_t block)
{
	uint8_t val = 0;
	w6300_read_buf(dev, offset, block, &val, 1);
	return val;
}

static void w6300_write_reg8(const struct device *dev, uint16_t offset, uint8_t block, uint8_t val)
{
	w6300_write_buf(dev, offset, block, &val, 1);
}

/* RX Polling Thread (Handles Init + RX) */
static void eth_w6300_rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	uint8_t reg;

	/* Wait for system to settle */
	k_sleep(K_SECONDS(3));

	LOG_INF("RX Thread: Starting W6300 Initialization...");

	/* Initialize Soft SPI */
	soft_spi_init(dev);
	k_sleep(K_MSEC(100));

	/* Soft reset */
	/* Mode Register (MR) at 0x0000 in Common Block */
	w6300_write_reg8(dev, W6300_REG_Sn_MR, W6300_BLOCK_COMMON, 0x80); 
	k_msleep(10);

	uint8_t ver = w6300_read_reg8(dev, W6300_VER, W6300_BLOCK_COMMON);
	LOG_INF("W6300 SW-SPI Version: 0x%02X", ver);

	if (eth_w6300_read_reg(dev, W6300_SYSR, &reg) == 0) {
		LOG_INF("W6300 SYSR=0x%02X", reg);
	}

	/* Init Socket 0 in MACRAW mode */
	eth_w6300_write_reg(dev, W6300_REG_Sn_MR, W6300_MR_MACRAW);
	eth_w6300_write_reg(dev, W6300_REG_Sn_CR, W6300_CR_OPEN);
	
	uint8_t cr;
	do {
		eth_w6300_read_reg(dev, W6300_REG_Sn_CR, &cr);
	} while (cr != 0);
	
	/* Check PHY Link Status */
	LOG_INF("RX Thread: Checking PHY Link...");
	for (int i = 0; i < 50; i++) {
		eth_w6300_read_reg(dev, W6300_PHYSR, &reg);
		if (reg & W6300_PHYSR_LNK) {
			LOG_INF("RX Thread: PHY Link Up! (PHYSR=0x%02X)", reg);
			break;
		}
		if (i % 10 == 0) LOG_WRN("RX Thread: PHY Link Down (PHYSR=0x%02X)...", reg);
		k_msleep(200);
	}

	while (1) {
		eth_w6300_rx(dev);
		k_msleep(2);
	}
}

static K_THREAD_STACK_DEFINE(rx_thread_stack, 2048);
static struct k_thread rx_thread_data;

static int eth_w6300_init(const struct device *dev)
{
	/* Only start the thread, do not touch HW here */
	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack),
			eth_w6300_rx_thread, (void *)dev, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

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
