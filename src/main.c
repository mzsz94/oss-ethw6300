#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(main);

#define HTTP_PORT 80

static const char *response =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<!DOCTYPE html>\r\n"
	"<html>\r\n"
	"<head><title>W6300 Web Server</title></head>\r\n"
	"<body>\r\n"
	"<h1>Hello from RP2350 + W6300!</h1>\r\n"
	"<p>This page is served by Zephyr OS.</p>\r\n"
	"</body>\r\n"
	"</html>\r\n";

static struct net_mgmt_event_callback mgmt_cb;

static void handler(struct net_mgmt_event_callback *cb,
		    uint64_t mgmt_event,
		    struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	LOG_INF("IPv4 address assigned.");
}
void start_http_server(void)
{
	int serv_sock;
	struct sockaddr_in bind_addr;
	int ret;

	serv_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serv_sock < 0) {
		LOG_ERR("Failed to create socket: %d", serv_sock);
		return;
	}

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(HTTP_PORT);

	ret = zsock_bind(serv_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind socket: %d", ret);
		zsock_close(serv_sock);
		return;
	}

	ret = zsock_listen(serv_sock, 5);
	if (ret < 0) {
		LOG_ERR("Failed to listen on socket: %d", ret);
		zsock_close(serv_sock);
		return;
	}

	LOG_INF("HTTP Server listening on port %d", HTTP_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client_sock;

		client_sock = zsock_accept(serv_sock, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_sock < 0) {
			LOG_ERR("Failed to accept connection");
			continue;
		}

		char addr_str[32];
		net_addr_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
		LOG_INF("Connection from %s", addr_str);

		/* Discard received data for simplicity, just send response */
		char recv_buf[512];
		int len = zsock_recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (len > 0) {
            recv_buf[len] = '\0';
            LOG_DBG("Received: %s", recv_buf);
        }

		zsock_send(client_sock, response, strlen(response), 0);
		zsock_close(client_sock);
		LOG_INF("Connection closed");
	}
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
	uint32_t dtr = 0;

	if (usb_enable(NULL)) {
		/* Failed to enable USB, but let's not exit. */
	}

	/* Wait for console connection */
	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	LOG_INF("W6300 Driver Test App Started");

	/* Subscribe to network events to show IP address */
	net_mgmt_init_event_callback(&mgmt_cb, handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	LOG_INF("Waiting for network connection...");

	LOG_INF("I LOVE YOU MANJAE!");

    /* Start the HTTP server */
	start_http_server();

	return 0;
}