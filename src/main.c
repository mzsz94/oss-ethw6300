#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#include "eth_w6300.h"

LOG_MODULE_REGISTER(main);

static void wait_for_dtr(const struct device *dev, uint32_t timeout_ms)
{
	uint32_t dtr = 0;
	int64_t start;

	if (!device_is_ready(dev)) {
		LOG_WRN("CDC ACM UART not ready, skipping DTR wait");
		return;
	}

	start = k_uptime_get();
	while (!dtr && (k_uptime_get() - start) < timeout_ms) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	if (!dtr) {
		LOG_WRN("DTR not asserted, continuing without host");
	}
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
	const struct device *const w6300 = DEVICE_DT_GET_ONE(wiznet_w6300);
	const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	uint8_t reg = 0;
	bool w6300_ready = false;
	int ret;

	/* 시리얼 터미널이 연결될 때까지 대기 (선택 사항)
	 * 맥북에서 포트가 바로 보이지 않는다면 이 루프가 도움이 됩니다.
	 */
	wait_for_dtr(dev, 5000);

	if (device_is_ready(gpio0)) {
		ret = gpio_pin_configure(gpio0, 20, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure W6300 IO2 GPIO");
		}
		ret = gpio_pin_configure(gpio0, 21, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure W6300 IO3 GPIO");
		}
		ret = gpio_pin_configure(gpio0, 22, GPIO_OUTPUT_INACTIVE | GPIO_ACTIVE_LOW);
		if (ret != 0) {
			LOG_ERR("Failed to configure W6300 RST GPIO");
		}
	} else {
		LOG_ERR("GPIO0 not ready");
	}

	w6300_ready = device_is_ready(w6300);
	if (!w6300_ready) {
		LOG_ERR("W6300 device not ready");
	} else {
		if (eth_w6300_read_reg(w6300, W6300_MR, &reg) == 0) {
			LOG_INF("W6300 MR=0x%02X", reg);
		}
		if (eth_w6300_read_reg(w6300, W6300_VERSIONR, &reg) == 0) {
			LOG_INF("W6300 VERSIONR=0x%02X", reg);
		}
	}

	if (w6300_ready && device_is_ready(gpio0)) {
		for (int level = 0; level <= 1; level++) {
			gpio_pin_set(gpio0, 20, level);
			gpio_pin_set(gpio0, 21, level);
			k_msleep(1);

			/* Assert reset (active low), then deassert */
			gpio_pin_set(gpio0, 22, 1);
			k_msleep(1);
			gpio_pin_set(gpio0, 22, 0);
			k_msleep(10);

			if (eth_w6300_read_reg(w6300, W6300_MR, &reg) == 0) {
				LOG_INF("IO2/IO3=%d -> W6300 MR=0x%02X", level, reg);
			}
			if (eth_w6300_read_reg(w6300, W6300_VERSIONR, &reg) == 0) {
				LOG_INF("IO2/IO3=%d -> W6300 VERSIONR=0x%02X", level, reg);
			}
		}
	}

	while (1)
	{
		if (w6300_ready && eth_w6300_read_reg(w6300, W6300_PHYCFGR, &reg) == 0) {
			const char *link = (reg & W6300_PHYCFGR_LNK) ? "up" : "down";
			const char *speed = (reg & W6300_PHYCFGR_SPD) ? "100" : "10";
			const char *duplex = (reg & W6300_PHYCFGR_DPX) ? "full" : "half";

			LOG_INF("W6300 PHY: link %s, %s Mbps, %s duplex (0x%02X)",
				link, speed, duplex, reg);
		} else {
			LOG_INF("W6300 Driver Test App alive");
		}
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
