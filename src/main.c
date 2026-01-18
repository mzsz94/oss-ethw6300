#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <errno.h>

LOG_MODULE_REGISTER(main);

#define HTTP_PORT 8080

static const char http_response[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<!DOCTYPE html><html><head><title>Pico2 W6300 Server</title></head>"
	"<body><h1>Hello, MZSZ!</h1>"
	"<p>This page is served by Zephyr RTOS and WIZnet W6300 Ethernet.</p>"
	"</body></html>";

void start_http_server(void)
{
	int serv;
	struct sockaddr_in bind_addr;
	struct net_if *iface;

	LOG_INF("Waiting for network interface...");
	iface = net_if_get_default();
	while (iface == NULL) {
		k_sleep(K_MSEC(100));
		iface = net_if_get_default();
	}

	while (!net_if_is_up(iface)) {
		k_sleep(K_MSEC(200));
	}

	while (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) == NULL) {
		k_sleep(K_MSEC(200));
	}

	LOG_INF("Network interface is up with IPv4 address assigned.");

	serv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(HTTP_PORT);
	bind_addr.sin_addr.s_addr = INADDR_ANY;

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

		LOG_INF("Connection accepted");
		char buf[256];
		zsock_recv(client, buf, sizeof(buf), 0);
		zsock_send(client, http_response, sizeof(http_response) - 1, 0);
		zsock_close(client);
	}
}

K_THREAD_DEFINE(http_server_thread, 2048, start_http_server, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
	uint32_t dtr = 0;
	int64_t wait_start;

	/* Wait for serial terminal connection */
	wait_start = k_uptime_get();
	while (!dtr && k_uptime_get() < (wait_start + 5000)) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
	if (!dtr) {
		LOG_WRN("USB serial not connected, continuing without DTR.");
	}

	LOG_INF("Hello from W6300 Driver Test App! For Risc-V");
	LOG_INF("Static IP: 192.168.0.200 (Check your router subnet!)");
	LOG_INF("W6300 Ethernet HTTP Server starting...");

	while (1)
	{
		k_sleep(K_SECONDS(10));
	}
	return 0;
}
