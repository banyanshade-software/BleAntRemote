#include "ant_garmin.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_INF);

void ant_garmin_init(void)
{
	LOG_WRN("ANT+ stack not implemented yet - Garmin buttons and "
		"temperature broadcast are stubs");
}

void ant_garmin_handle_button(const char *which)
{
	LOG_WRN("Garmin button '%s' pressed - ANT+ not implemented yet", which);
}

void ant_garmin_send_temperature(int16_t temp_centi)
{
	LOG_WRN("Temperature broadcast (%d.%02d C) not sent - ANT+ not "
		"implemented yet", temp_centi / 100, abs(temp_centi % 100));
}
