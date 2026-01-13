#define DT_DRV_COMPAT wiznet_w6300

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(eth_w6300, CONFIG_ETHERNET_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/ethernet.h>

#include "eth_w6300.h"

static int eth_w6300_init(const struct device *dev)
{
	LOG_INF("W6300 Driver Initialized");
	return 0;
}

static const struct ethernet_api eth_w6300_api = {
	/* We will fill this later */
};

/* 
 * This macro defines the device instance. 
 * It connects the code to the hardware defined in Device Tree.
 */
#define ETH_W6300_DEFINE(n) \
	DEVICE_DT_INST_DEFINE(n, \
		eth_w6300_init, \
		NULL, \
		NULL, \
		NULL, \
		POST_KERNEL, \
		CONFIG_ETH_INIT_PRIORITY, \
		&eth_w6300_api);

DT_INST_FOREACH_STATUS_OKAY(ETH_W6300_DEFINE)
