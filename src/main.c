#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

int main(void)
{
	LOG_INF("Hello from W6300 Driver Test App!");
	while (1)
	{
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
