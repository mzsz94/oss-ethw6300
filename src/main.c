#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

/* Network headers commented out */
// #include <zephyr/net/net_if.h>
// #include <zephyr/net/net_core.h>
// #include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(main);

#define HTTP_PORT 8080

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
	uint32_t dtr = 0;

	if (usb_enable(NULL)) {
		return 0;
	}

	/* Wait for serial terminal connection */
	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	LOG_INF("System Alive! Network Disabled.");

	while (1)
	{
		LOG_INF("Tick...");
		k_sleep(K_SECONDS(1));
	}
	return 0;
}