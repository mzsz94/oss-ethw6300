#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

LOG_MODULE_REGISTER(main);

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
	uint32_t dtr = 0;

	if (usb_enable(NULL)) {
		return 0;
	}

	/* 시리얼 터미널이 연결될 때까지 대기 (선택 사항)
	 * 맥북에서 포트가 바로 보이지 않는다면 이 루프가 도움이 됩니다.
	 */
	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	while (1)
	{
		LOG_INF("Hello from W6300 Driver Test App! For Risc-V");
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
