#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
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
	uint8_t reg = 0;
	bool w6300_ready = false;

	/* 시리얼 터미널이 연결될 때까지 대기 (선택 사항)
	 * 맥북에서 포트가 바로 보이지 않는다면 이 루프가 도움이 됩니다.
	 */
	wait_for_dtr(dev, 5000);

	w6300_ready = device_is_ready(w6300);
	if (!w6300_ready) {
		LOG_ERR("W6300 device not ready");
	} else {
		uint8_t cidr0 = 0;
		uint8_t cidr1 = 0;

		if (eth_w6300_read_reg(w6300, W6300_CIDR0, &cidr0) == 0) {
			LOG_INF("W6300 CIDR0=0x%02X", cidr0);
		}
		if (eth_w6300_read_reg(w6300, W6300_CIDR1, &cidr1) == 0) {
			LOG_INF("W6300 CIDR1=0x%02X", cidr1);
		}
		if (eth_w6300_read_reg(w6300, W6300_VER, &reg) == 0) {
			LOG_INF("W6300 VER=0x%02X", reg);
		}
	}

	while (1)
	{
		if (w6300_ready && eth_w6300_read_reg(w6300, W6300_PHYSR, &reg) == 0) {
			const char *link = (reg & W6300_PHYSR_LNK) ? "up" : "down";
			const char *speed = (reg & W6300_PHYSR_SPD) ? "100" : "10";
			const char *duplex = (reg & W6300_PHYSR_DPX) ? "full" : "half";

			LOG_INF("W6300 PHY: link %s, %s Mbps, %s duplex (0x%02X)",
				link, speed, duplex, reg);
		} else {
			LOG_INF("W6300 Driver Test App alive");
		}
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
