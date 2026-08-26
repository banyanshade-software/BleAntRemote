/*
 * temp_sensor - reads the nRF52832's internal die temperature sensor
 * ======================================================================
 * Wraps the SoC's built-in TEMP peripheral (no external thermistor
 * needed) behind a small, protocol-agnostic interface. Its output is
 * meant to be broadcast over ANT+ by ant_garmin_send_temperature() -
 * this module only knows how to read the sensor, not how it is
 * transmitted, per the project's modularity requirement (see
 * doc/gopro_garmin_remote_specs.md, "Maintainability & modularity").
 *
 * Note: measures the chip's die temperature, not true free-air ambient
 * temperature - expect an offset from self-heating/enclosure. See the
 * "Internal temperature sensor" section of the specs doc for details.
 */
#ifndef TEMP_SENSOR_H_
#define TEMP_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Initializes the internal temperature sensor device. Returns 0 on
 * success, a negative errno if the sensor device isn't available.
 */
int temp_sensor_init(void);

/*
 * Reads the current die temperature, in hundredths of a degree Celsius
 * (e.g. 2153 means 21.53 C). Returns true and writes *out_temp_centi on
 * success, false on read failure.
 */
bool temp_sensor_read(int16_t *out_temp_centi);

#endif /* TEMP_SENSOR_H_ */
