#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(main);

#define HTTP_PORT 80

static const char http_response[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<!DOCTYPE html><html><head><title>Pico2 W6300 Server</title></head>"
	"<body><h1>Hello from Pico2 RISC-V (Hazard3)</h1>"
	"<p>This page is served by Zephyr RTOS and WIZnet W6300 Ethernet.</p>"
	"</body></html>";

void start_http_server(void)
{
	int serv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(HTTP_PORT),
		.sin_addr.s_addr = INADDR_ANY,
	};

	if (serv < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return;
	}

	if (zsock_bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		LOG_ERR("Failed to bind socket: %d", errno);
		zsock_close(serv);
		return;
	}

	if (zsock_listen(serv, 5) < 0) {
		LOG_ERR("Failed to listen on socket: %d", errno);
		zsock_close(serv);
		return;
	}

	LOG_INF("HTTP Server listening on port %d...", HTTP_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client = zsock_accept(serv, (struct sockaddr *)&client_addr, &client_addr_len);

		if (client < 0) {
			LOG_ERR("Failed to accept connection: %d", errno);
			continue;
		}

		char buf[256];
		zsock_recv(client, buf, sizeof(buf), 0);
		/* Simple server: ignore request content and just send response */
		zsock_send(client, http_response, sizeof(http_response) - 1, 0);
		zsock_close(client);
	}
}

K_THREAD_DEFINE(http_server_thread, 2048, start_http_server, NULL, NULL, NULL, 7, 0, 0);

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

	LOG_INF("Hello from W6300 Driver Test App! For Risc-V");
	LOG_INF("W6300 Ethernet HTTP Server started.");

	while (1)
	{
		k_sleep(K_SECONDS(10));
	}
	return 0;
}
