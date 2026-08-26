/*
 * NOTE: like the rest of this firmware, this has not been built/tested
 * against a real nRF Connect SDK checkout in this environment (see
 * software/gopro_remote_fw/README.md). This follows Zephyr's standard
 * sensor API for the nRF52's SoC-internal die temperature sensor
 * (devicetree node compatible "nordic,nrf-temp", driver enabled via
 * CONFIG_SENSOR=y). Double-check the exact devicetree node label and
 * Kconfig symbols against the nRF Connect SDK version you build with -
 * they were not verified against a real build here.
 */
#include "temp_sensor.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(temp_sensor, LOG_LEVEL_INF);

#define TEMP_SENSOR_NODE DT_NODELABEL(temp)

static const struct device *const temp_dev =
	DEVICE_DT_GET_OR_NULL(TEMP_SENSOR_NODE);

int temp_sensor_init(void)
{
	if (temp_dev == NULL || !device_is_ready(temp_dev)) {
		LOG_ERR("Internal temperature sensor device not ready");
		return -ENODEV;
	}
	return 0;
}

bool temp_sensor_read(int16_t *out_temp_centi)
{
	struct sensor_value val;

	if (temp_dev == NULL) {
		return false;
	}

	if (sensor_sample_fetch(temp_dev) < 0) {
		LOG_WRN("Failed to fetch temperature sample");
		return false;
	}
	if (sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val) < 0) {
		LOG_WRN("Failed to read temperature channel");
		return false;
	}

	*out_temp_centi = (int16_t)(val.val1 * 100 + val.val2 / 10000);
	LOG_INF("Die temperature: %d.%02d C", val.val1,
		val.val2 < 0 ? -val.val2 / 10000 : val.val2 / 10000);
	return true;
}
