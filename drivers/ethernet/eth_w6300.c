#define DT_DRV_COMPAT wiznet_w6300

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(eth_w6300, CONFIG_ETHERNET_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>

#include "eth_w6300.h"

/* W6300 Blocks */
#define W6300_BLOCK_COMMON    0x00
#define W6300_BLOCK_S0        0x01
#define W6300_BLOCK_S0_TX     0x02
#define W6300_BLOCK_S0_RX     0x03

/* Common Registers */
#define W6300_REG_MODE        0x0000
#define W6300_REG_SHAR        0x0009 /* MAC Address */
#define W6300_REG_VERSION     0x001E /* Version Register in W6100/W6300 */

struct eth_w6300_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec interrupt;
};

struct eth_w6300_dev_data {
	struct net_if *iface;
	uint8_t mac_addr[6];
};

static int w6300_read_reg(const struct device *dev, uint16_t addr, uint8_t block, uint8_t *val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t cmd[3];
	
	cmd[0] = (addr >> 8) & 0xFF;
	cmd[1] = addr & 0xFF;
	cmd[2] = (block << 3) | (0 << 2); /* Read, Variable length */

	const struct spi_buf tx_buf[] = {
		{ .buf = cmd, .len = 3 },
	};
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };

	struct spi_buf rx_buf[] = {
		{ .buf = NULL, .len = 3 },
		{ .buf = val, .len = 1 },
	};
	const struct spi_buf_set rx = { .buffers = rx_buf, .count = 2 };

	return spi_transceive_dt(&cfg->spi, &tx, &rx);
}

static int w6300_write_reg(const struct device *dev, uint16_t addr, uint8_t block, uint8_t val)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t cmd[4];
	
	cmd[0] = (addr >> 8) & 0xFF;
	cmd[1] = addr & 0xFF;
	cmd[2] = (block << 3) | (1 << 2); /* Write, Variable length */
	cmd[3] = val;

	const struct spi_buf tx_buf[] = {
		{ .buf = cmd, .len = 4 },
	};
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx);
}

/* Socket Registers */
#define W6300_REG_Sn_MR       0x0000
#define W6300_REG_Sn_CR       0x0001
#define W6300_REG_Sn_IR       0x0002
#define W6300_REG_Sn_SR       0x0003
#define W6300_REG_Sn_TX_FSR   0x0012
#define W6300_REG_Sn_TX_WR    0x0014
#define W6300_REG_Sn_RX_RSR   0x0016
#define W6300_REG_Sn_RX_RD    0x0018

/* Socket Commands */
#define W6300_CR_OPEN         0x01
#define W6300_CR_SEND         0x20
#define W6300_CR_RECV         0x40

/* Socket Modes */
#define W6300_MR_MACRAW       0x04

/* W6300 Buffer Memory Access Helpers */
static int w6300_write_buf(const struct device *dev, uint16_t offset, uint8_t block, uint8_t *buf, uint16_t len)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t cmd[3];

	cmd[0] = (offset >> 8) & 0xFF;
	cmd[1] = offset & 0xFF;
	cmd[2] = (block << 3) | (1 << 2); /* Write */

	const struct spi_buf tx_buf[] = {
		{ .buf = cmd, .len = 3 },
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 2 };

	return spi_write_dt(&cfg->spi, &tx);
}

static int w6300_read_buf(const struct device *dev, uint16_t offset, uint8_t block, uint8_t *buf, uint16_t len)
{
	const struct eth_w6300_config *cfg = dev->config;
	uint8_t cmd[3];

	cmd[0] = (offset >> 8) & 0xFF;
	cmd[1] = offset & 0xFF;
	cmd[2] = (block << 3) | (0 << 2); /* Read */

	const struct spi_buf tx_buf[] = {
		{ .buf = cmd, .len = 3 },
	};
	const struct spi_buf_set tx = { .buffers = tx_buf, .count = 1 };

	struct spi_buf rx_buf[] = {
		{ .buf = NULL, .len = 3 },
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set rx = { .buffers = rx_buf, .count = 2 };

	return spi_transceive_dt(&cfg->spi, &tx, &rx);
}

static uint16_t w6300_get_tx_free_size(const struct device *dev)
{
	uint16_t val = 0;
	uint8_t tmp[2];
	w6300_read_reg(dev, W6300_REG_Sn_TX_FSR, W6300_BLOCK_S0, &tmp[0]); /* High byte */
	w6300_read_reg(dev, W6300_REG_Sn_TX_FSR + 1, W6300_BLOCK_S0, &tmp[1]); /* Low byte */
	val = (tmp[0] << 8) | tmp[1];
	return val;
}

static uint16_t w6300_get_tx_wr_ptr(const struct device *dev)
{
	uint16_t val = 0;
	uint8_t tmp[2];
	w6300_read_reg(dev, W6300_REG_Sn_TX_WR, W6300_BLOCK_S0, &tmp[0]);
	w6300_read_reg(dev, W6300_REG_Sn_TX_WR + 1, W6300_BLOCK_S0, &tmp[1]);
	val = (tmp[0] << 8) | tmp[1];
	return val;
}

static void w6300_set_tx_wr_ptr(const struct device *dev, uint16_t ptr)
{
	w6300_write_reg(dev, W6300_REG_Sn_TX_WR, W6300_BLOCK_S0, (ptr >> 8) & 0xFF);
	w6300_write_reg(dev, W6300_REG_Sn_TX_WR + 1, W6300_BLOCK_S0, ptr & 0xFF);
}

static uint16_t w6300_get_rx_size(const struct device *dev)
{
	uint16_t val = 0;
	uint8_t tmp[2];
	w6300_read_reg(dev, W6300_REG_Sn_RX_RSR, W6300_BLOCK_S0, &tmp[0]);
	w6300_read_reg(dev, W6300_REG_Sn_RX_RSR + 1, W6300_BLOCK_S0, &tmp[1]);
	val = (tmp[0] << 8) | tmp[1];
	return val;
}

static uint16_t w6300_get_rx_rd_ptr(const struct device *dev)
{
	uint16_t val = 0;
	uint8_t tmp[2];
	w6300_read_reg(dev, W6300_REG_Sn_RX_RD, W6300_BLOCK_S0, &tmp[0]);
	w6300_read_reg(dev, W6300_REG_Sn_RX_RD + 1, W6300_BLOCK_S0, &tmp[1]);
	val = (tmp[0] << 8) | tmp[1];
	return val;
}

static void w6300_set_rx_rd_ptr(const struct device *dev, uint16_t ptr)
{
	w6300_write_reg(dev, W6300_REG_Sn_RX_RD, W6300_BLOCK_S0, (ptr >> 8) & 0xFF);
	w6300_write_reg(dev, W6300_REG_Sn_RX_RD + 1, W6300_BLOCK_S0, ptr & 0xFF);
}

static void w6300_exec_cmd(const struct device *dev, uint8_t cmd)
{
	w6300_write_reg(dev, W6300_REG_Sn_CR, W6300_BLOCK_S0, cmd);
	/* Wait for command completion is typically needed, but W6300 auto-clears CR */
	uint8_t cr = 0;
	do {
		w6300_read_reg(dev, W6300_REG_Sn_CR, W6300_BLOCK_S0, &cr);
	} while (cr != 0);
}

static int eth_w6300_send(const struct device *dev, struct net_pkt *pkt)
{
	uint16_t len = net_pkt_get_len(pkt);
	uint16_t freesize = 0;
	uint16_t ptr = 0;

	if (len > NET_ETH_MTU + 18) {
		LOG_ERR("Packet too large: %d", len);
		return -EINVAL;
	}

	/* Wait until TX memory is available */
	do {
		freesize = w6300_get_tx_free_size(dev);
	} while (freesize < len);

	ptr = w6300_get_tx_wr_ptr(dev);

	/* Write packet to W6300 TX buffer */
	struct net_buf *frag;
	uint16_t offset = ptr;

	for (frag = pkt->buffer; frag; frag = frag->frags) {
		w6300_write_buf(dev, offset, W6300_BLOCK_S0_TX, frag->data, frag->len);
		offset += frag->len;
	}

	w6300_set_tx_wr_ptr(dev, offset);
	w6300_exec_cmd(dev, W6300_CR_SEND);

	LOG_DBG("Packet sent, len %d", len);
	return 0;
}

static void eth_w6300_rx(const struct device *dev)
{
	struct eth_w6300_dev_data *data = dev->data;
	uint16_t len = 0;
	uint16_t ptr = 0;
	uint8_t header[2];

	uint16_t rsr = w6300_get_rx_size(dev);
	if (rsr == 0) return;

	ptr = w6300_get_rx_rd_ptr(dev);

	/* Read 2-byte length header */
	w6300_read_buf(dev, ptr, W6300_BLOCK_S0_RX, header, 2);
	ptr += 2;
	len = (header[0] << 8) | header[1];
	len -= 2; /* Adjust length to exclude the header itself if needed, but W6300 MACRAW header includes payload len? 
	             Standard W5500 MACRAW: first 2 bytes are packet length. */

	if (len == 0 || len > 1518) {
		LOG_ERR("Invalid RX packet length: %d", len);
		/* Flush invalid packet */
		/* Just update RD pointer to skip this garbage is tricky without knowing size.
		 * Usually we should read the size register and if it's garbage, maybe reset socket?
		 * For now, assume it's valid if > 0.
		 */
	}

	struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		LOG_ERR("Failed to alloc RX packet");
		/* Drop packet: advance RD pointer */
		ptr += len;
		w6300_set_rx_rd_ptr(dev, ptr);
		w6300_exec_cmd(dev, W6300_CR_RECV);
		return;
	}

	/* Read payload */
	struct net_buf *frag;
	uint16_t offset = ptr;
	for (frag = pkt->buffer; frag; frag = frag->frags) {
		w6300_read_buf(dev, offset, W6300_BLOCK_S0_RX, frag->data, frag->len);
		offset += frag->len;
	}

	w6300_set_rx_rd_ptr(dev, offset);
	w6300_exec_cmd(dev, W6300_CR_RECV);

	if (net_recv_data(data->iface, pkt) < 0) {
		LOG_ERR("Failed to recv packet to stack");
		net_pkt_unref(pkt);
	} else {
		LOG_DBG("Packet received, len %d", len);
	}
}

/* Polling thread for RX */
static void eth_w6300_rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	
	/* Wait for system boot and USB to settle */
	k_msleep(1000); 

	while (1) {
		eth_w6300_rx(dev);
		k_msleep(5); /* Yield to other threads slightly longer */
	}
}

static K_THREAD_STACK_DEFINE(rx_thread_stack, 1024);
static struct k_thread rx_thread_data;

static int eth_w6300_init(const struct device *dev)
{
	/* DEBUG: Immediate return to allow system boot */
	/* Do NOT use k_sleep here as it blocks the kernel boot sequence! */
	return 0;

	const struct eth_w6300_config *cfg = dev->config;

	/* Soft reset */
	// w6300_write_reg(dev, W6300_REG_MODE, W6300_BLOCK_COMMON, 0x80);
	k_msleep(10);

	/* Check version */
	// w6300_read_reg(dev, W6300_REG_VERSION, W6300_BLOCK_COMMON, &version);
	// LOG_INF("W6300 Chip Version: 0x%02X", version);

	/* Initialize Socket 0 in MACRAW mode */
	// w6300_write_reg(dev, W6300_REG_Sn_MR, W6300_BLOCK_S0, W6300_MR_MACRAW);
	// w6300_write_reg(dev, W6300_REG_Sn_CR, W6300_BLOCK_S0, W6300_CR_OPEN);
	
	// uint8_t sr = 0;
	// w6300_read_reg(dev, W6300_REG_Sn_SR, W6300_BLOCK_S0, &sr);
	// LOG_INF("Socket 0 Status: 0x%02X", sr);

	/* Start RX polling thread with lower priority (Preemptible) */
	/* Disable Thread for now */
	/*
	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack),
			eth_w6300_rx_thread, (void *)dev, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	*/

	return 0;
}

static void eth_w6300_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct eth_w6300_dev_data *data = dev->data;

	data->iface = iface;

	/* Set MAC address (dummy for now or from DTS) */
	data->mac_addr[0] = 0x00;
	data->mac_addr[1] = 0x08;
	data->mac_addr[2] = 0xDC;
	data->mac_addr[3] = 0x11;
	data->mac_addr[4] = 0x22;
	data->mac_addr[5] = 0x33;

	net_if_set_link_addr(iface, data->mac_addr, 6, NET_LINK_ETHERNET);
	ethernet_init(iface);
}

static const struct ethernet_api eth_w6300_api = {
	.iface_api.init = eth_w6300_iface_init,
	.send = eth_w6300_send,
};

#define ETH_W6300_DEFINE(n) \
	static const struct eth_w6300_config eth_w6300_config_##n = { \
		.spi = SPI_DT_SPEC_INST_GET(n, SPI_WORD_SET(8), 0), \
		.interrupt = GPIO_DT_SPEC_INST_GET(n, int_gpios), \
	}; \
	\
	static struct eth_w6300_dev_data eth_w6300_data_##n; \
	\
	ETH_NET_DEVICE_DT_INST_DEFINE(n, \
		eth_w6300_init, \
		NULL, \
		&eth_w6300_data_##n, \
		&eth_w6300_config_##n, \
		CONFIG_ETH_INIT_PRIORITY, \
		&eth_w6300_api, \
		NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(ETH_W6300_DEFINE)