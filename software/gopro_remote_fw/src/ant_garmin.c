#include "ant_garmin.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_INF);

void ant_garmin_init(void)
{
	LOG_WRN("ANT+ stack not implemented yet - Garmin buttons are stubs");
}

void ant_garmin_handle_button(const char *which)
{
	LOG_WRN("Garmin button '%s' pressed - ANT+ not implemented yet", which);
}
